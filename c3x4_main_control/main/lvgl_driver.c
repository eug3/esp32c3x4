/**
 * @file lvgl_driver.c
 * @brief LVGL驱动适配层 - EPD和按键输入 (LVGL 9.x)
 */

#include "lvgl_driver.h"
#include "EPD_4in26.h"
#include "GUI_Paint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "LVGL_DRV";

// EPD显示尺寸
// 物理屏幕为 800x480，但旧版 welcome 使用 ROTATE_270 形成竖屏逻辑坐标 480x800。
// 为了沿用旧版竖向布局，这里让 LVGL 也工作在 480x800 的逻辑分辨率。
#define EPD_WIDTH       800
#define EPD_HEIGHT      480
#define DISP_HOR_RES    480
#define DISP_VER_RES    800
#define DISP_BUF_LINES  5

// 显示缓冲区
static lv_color_t buf1[DISP_HOR_RES * DISP_BUF_LINES];

// 1bpp framebuffer for EPD (directly operated by LVGL)
// 使用静态分配，避免依赖外部的 BlackImage
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];

// Track the union of flushed areas since last EPD refresh.
static lv_area_t s_dirty_area;
static bool s_dirty_valid = false;

// EPD refresh state tracking (异步刷新)
static bool s_epd_refreshing = false;
static SemaphoreHandle_t s_epd_mutex = NULL;
static TaskHandle_t s_epd_refresh_task_handle = NULL;
static QueueHandle_t s_refresh_queue = NULL;

// 刷新请求结构
typedef struct {
    bool force_full;  // 是否强制全刷
} refresh_request_t;

// Partial refresh counter: force full refresh every N partial refreshes
static uint32_t s_partial_refresh_count = 0;
#define FORCE_FULL_REFRESH_AFTER_N_PARTIAL 5

// 优化：不再使用单独的局部刷新缓冲区
// 改为流式发送，直接从 s_epd_framebuffer 按行发送数据
// 节省内存：约 12 KB

static void dirty_area_add(const lv_area_t *area)
{
    if (!s_dirty_valid) {
        s_dirty_area = *area;
        s_dirty_valid = true;
        ESP_LOGD(TAG, "New dirty area: x=[%d, %d] y=[%d, %d]", 
                 (int)area->x1, (int)area->x2, (int)area->y1, (int)area->y2);
        return;
    }

    // Record old bounds for comparison
    lv_area_t old_area = s_dirty_area;

    if (area->x1 < s_dirty_area.x1) s_dirty_area.x1 = area->x1;
    if (area->y1 < s_dirty_area.y1) s_dirty_area.y1 = area->y1;
    if (area->x2 > s_dirty_area.x2) s_dirty_area.x2 = area->x2;
    if (area->y2 > s_dirty_area.y2) s_dirty_area.y2 = area->y2;

    // Log if area grew
    if (s_dirty_area.x1 != old_area.x1 || s_dirty_area.y1 != old_area.y1 ||
        s_dirty_area.x2 != old_area.x2 || s_dirty_area.y2 != old_area.y2) {
        ESP_LOGD(TAG, "Dirty area union: new=[%d,%d,%d,%d] old=[%d,%d,%d,%d]",
                 (int)area->x1, (int)area->x2, (int)area->y1, (int)area->y2,
                 (int)old_area.x1, (int)old_area.x2, (int)old_area.y1, (int)old_area.y2);
    }
}

// 直接写入 1bpp framebuffer 的辅助函数
// 坐标映射：LVGL 逻辑坐标 (x, y) -> 物理屏幕坐标 (memX, memY) 使用 ROTATE_270
// EPD 1bpp 格式: 0=黑色, 1=白色（标准电子墨水屏格式）
static inline void set_epd_pixel(int32_t x, int32_t y, bool black)
{
    if (x < 0 || x >= DISP_HOR_RES || y < 0 || y >= DISP_VER_RES) return;

    // ROTATE_270 映射: memX = y, memY = EPD_HEIGHT - 1 - x
    const int32_t memX = y;
    const int32_t memY = EPD_HEIGHT - 1 - x;

    // Bounds check for framebuffer
    if (memX < 0 || memX >= EPD_WIDTH || memY < 0 || memY >= EPD_HEIGHT) {
        ESP_LOGE(TAG, "set_epd_pixel: out of bounds - LVGL(x=%d, y=%d) -> Phys(memX=%d, memY=%d)",
                 (int)x, (int)y, (int)memX, (int)memY);
        return;
    }

    const uint32_t byte_index = (uint32_t)memY * (EPD_WIDTH / 8) + (uint32_t)(memX / 8);
    const uint8_t bit_mask = 1 << (7 - (memX % 8));

    // Bounds check for framebuffer index
    if (byte_index >= sizeof(s_epd_framebuffer)) {
        ESP_LOGE(TAG, "set_epd_pixel: framebuffer index out of bounds - byte_index=%u (max=%u)",
                 (unsigned)byte_index, (unsigned)sizeof(s_epd_framebuffer));
        return;
    }

    if (black) {
        s_epd_framebuffer[byte_index] &= ~bit_mask; // Clear bit = black (0)
    } else {
        s_epd_framebuffer[byte_index] |= bit_mask;  // Set bit = white (1)
    }
}

// LVGL 9.x 显示flush回调
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x, y;

    // 如果 EPD 正在刷新，跳过这次 flush（避免数据竞争）
    if (s_epd_refreshing) {
        ESP_LOGW(TAG, "EPD is refreshing, skipping flush");
        lv_display_flush_ready(disp);
        return;
    }

    // 获取显示颜色格式
    lv_color_format_t cf = lv_display_get_color_format(disp);

    // 获取缓冲区宽度（stride）
    int32_t buf_w = lv_area_get_width(area);

    // Bounds check for area
    if (area->x1 < 0 || area->y1 < 0 || area->x2 >= DISP_HOR_RES || area->y2 >= DISP_VER_RES) {
        ESP_LOGW(TAG, "disp_flush_cb: area out of bounds - x1=%d, y1=%d, x2=%d, y2=%d (max=%dx%d)",
                 (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                 DISP_HOR_RES, DISP_VER_RES);
    }

    // 将 LVGL 的缓冲区数据直接写入到 1bpp framebuffer。
    // 坐标为 LVGL 逻辑坐标（竖屏 480x800）。
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            // 计算在缓冲区中的位置
            int32_t buf_x = x - area->x1;
            int32_t buf_y = y - area->y1;
            uint32_t buf_pos = buf_y * buf_w + buf_x;

            // 根据颜色格式获取像素
            bool black = false;
            if (cf == LV_COLOR_FORMAT_RGB565) {
                uint16_t *pixel_16 = (uint16_t *)px_map;
                uint16_t c = pixel_16[buf_pos];
                const uint8_t r5 = (c >> 11) & 0x1F;
                const uint8_t g6 = (c >> 5) & 0x3F;
                const uint8_t b5 = (c >> 0) & 0x1F;
                const uint8_t r8 = (uint8_t)((r5 * 255) / 31);
                const uint8_t g8 = (uint8_t)((g6 * 255) / 63);
                const uint8_t b8 = (uint8_t)((b5 * 255) / 31);
                const uint16_t lum = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11);
                const uint8_t y8 = (uint8_t)(lum / 100);
                // 阈值：亮->白，暗->黑
                black = (y8 < 128);
            } else if (cf == LV_COLOR_FORMAT_XRGB8888 || cf == LV_COLOR_FORMAT_ARGB8888) {
                uint32_t *pixel_32 = (uint32_t *)px_map;
                uint32_t c = pixel_32[buf_pos];
                const uint8_t r8 = (c >> 16) & 0xFF;
                const uint8_t g8 = (c >> 8) & 0xFF;
                const uint8_t b8 = (c >> 0) & 0xFF;
                const uint16_t lum = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11);
                const uint8_t y8 = (uint8_t)(lum / 100);
                black = (y8 < 128);
            }
            set_epd_pixel(x, y, black);
        }
    }

    // Record dirty area for partial EPD refresh later.
    dirty_area_add(area);

    // 通知LVGL刷新完成
    lv_display_flush_ready(disp);
}

// 异步 EPD 刷新任务（前置声明）
static void epd_refresh_task(void *arg);

// 初始化LVGL显示驱动
lv_display_t* lvgl_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL display driver (LVGL 9.x)");

    // 创建互斥锁（用于保护 framebuffer 访问）
    if (s_epd_mutex == NULL) {
        s_epd_mutex = xSemaphoreCreateMutex();
        if (s_epd_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create EPD mutex!");
            return NULL;
        }
    }

    // 创建刷新请求队列（异步刷新）
    if (s_refresh_queue == NULL) {
        s_refresh_queue = xQueueCreate(2, sizeof(refresh_request_t));
        if (s_refresh_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create refresh queue!");
            return NULL;
        }
    }

    // 创建异步刷新任务
    if (s_epd_refresh_task_handle == NULL) {
        BaseType_t ret = xTaskCreate(
            epd_refresh_task,
            "epd_refresh",
            4096,
            NULL,
            3,  // 优先级高于 LVGL 任务 (2)
            &s_epd_refresh_task_handle
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create EPD refresh task!");
            return NULL;
        }
        ESP_LOGI(TAG, "EPD refresh task created (async mode)");
    }

    // 清空framebuffer并初始化为白色（EPD格式: 1=白色, 0=黑色）
    memset(s_epd_framebuffer, 0xFF, sizeof(s_epd_framebuffer));
    ESP_LOGI(TAG, "EPD framebuffer cleared and initialized to white (%u bytes)",
             (unsigned)sizeof(s_epd_framebuffer));

    // 清空脏区域追踪
    s_dirty_valid = false;
    s_partial_refresh_count = 0;
    s_epd_refreshing = false;

    // 初始化LVGL
    lv_init();

    // 创建显示设备 - LVGL 9.x 新API
    lv_display_t *disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush_cb);

    // 设置缓冲区 - LVGL 9.x 使用字节数而不是像素数
    uint32_t buf_size = DISP_HOR_RES * DISP_BUF_LINES * sizeof(lv_color_t);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL display driver initialized (logical): %dx%d",
             DISP_HOR_RES, DISP_VER_RES);

    return disp;
}

// 异步 EPD 刷新任务
static void epd_refresh_task(void *arg)
{
    ESP_LOGI(TAG, "EPD refresh task started");

    while (1) {
        refresh_request_t req;
        // 等待刷新请求，阻塞等待
        if (xQueueReceive(s_refresh_queue, &req, portMAX_DELAY) == pdTRUE) {
            // 执行实际的 EPD 刷新
            if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_epd_refreshing = true;
                __sync_synchronize();

                if (s_dirty_valid) {
                    // ... (刷新逻辑，与原来相同)
                    ESP_LOGI(TAG, "=== Refresh Task ===");

                    // 坐标映射和对齐（从原函数复制）
                    int32_t mem_x1 = s_dirty_area.y1;
                    int32_t mem_x2 = s_dirty_area.y2;
                    int32_t mem_y1 = (EPD_HEIGHT - 1) - s_dirty_area.x2;
                    int32_t mem_y2 = (EPD_HEIGHT - 1) - s_dirty_area.x1;
                    if (mem_y1 > mem_y2) {
                        const int32_t tmp = mem_y1; mem_y1 = mem_y2; mem_y2 = tmp;
                    }

                    mem_x1 = mem_x1 & ~0x7;
                    mem_x2 = ((mem_x2 + 7) & ~0x7) - 1;

                    if (mem_x1 < 0) mem_x1 = 0;
                    if (mem_y1 < 0) mem_y1 = 0;
                    if (mem_x2 >= EPD_WIDTH) mem_x2 = EPD_WIDTH - 1;
                    if (mem_y2 >= EPD_HEIGHT) mem_y2 = EPD_HEIGHT - 1;

                    const uint32_t mem_w = (uint32_t)(mem_x2 - mem_x1 + 1);
                    const uint32_t mem_h = (uint32_t)(mem_y2 - mem_y1 + 1);
                    const uint32_t screen_area = (uint32_t)EPD_WIDTH * (uint32_t)EPD_HEIGHT;
                    const uint32_t dirty_area = mem_w * mem_h;

                    bool force_full = req.force_full || (s_partial_refresh_count >= FORCE_FULL_REFRESH_AFTER_N_PARTIAL);

                    if (dirty_area > (screen_area * 3) / 5 || force_full) {
                        ESP_LOGI(TAG, "Full refresh (async)");
                        EPD_4in26_Display(s_epd_framebuffer);
                        s_partial_refresh_count = 0;
                    } else {
                        // 局部刷新
                        const UWORD x0 = (UWORD)mem_x1;
                        const UWORD y0 = (UWORD)mem_y1;
                        const UWORD w0 = (UWORD)mem_w;
                        const UWORD h0 = (UWORD)mem_h;
                        const UWORD row_stride = (UWORD)((w0 + 7) / 8);
                        const uint32_t fb_stride = (uint32_t)(EPD_WIDTH / 8);
                        const uint32_t x_byte_offset = (uint32_t)(x0 / 8);

                        ESP_LOGI(TAG, "Partial refresh (async): %ux%u at (%u,%u)", w0, h0, x0, y0);
                        EPD_4in26_Display_Part_Stream(s_epd_framebuffer, x0, y0, w0, h0,
                                                      fb_stride, x_byte_offset, row_stride);
                        s_partial_refresh_count++;
                    }

                    s_dirty_valid = false;
                } else {
                    // 无脏区域，全刷
                    ESP_LOGI(TAG, "Full refresh (async, no dirty area)");
                    EPD_4in26_Display(s_epd_framebuffer);
                    s_partial_refresh_count = 0;
                }

                s_epd_refreshing = false;
                xSemaphoreGive(s_epd_mutex);

                ESP_LOGI(TAG, "EPD refresh completed (async)");
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for refresh");
            }
        }
    }
}

// 强制完整刷新EPD (改为异步请求 - 非阻塞)
void lvgl_display_refresh(void)
{
    refresh_request_t req = {
        .force_full = false
    };

    // 发送刷新请求到队列，立即返回（非阻塞）
    if (xQueueSend(s_refresh_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Refresh queue full, skipping refresh request");
    } else {
        ESP_LOGD(TAG, "Refresh request queued (async)");
    }
}

// 强制完整刷新EPD (异步请求 - 非阻塞)
void lvgl_display_refresh_full(void)
{
    refresh_request_t req = {
        .force_full = true
    };

    // 发送刷新请求到队列，立即返回（非阻塞）
    if (xQueueSend(s_refresh_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Refresh queue full, skipping full refresh request");
    } else {
        ESP_LOGI(TAG, "Full refresh request queued (async)");
    }
}

/* ========================================================================
 * 输入设备驱动 - 按键 (LVGL 9.x)
 * ========================================================================*/

// 按键防抖配置
#define KEY_REPEAT_DELAY_MS 300   // 按住后首次重复的延迟
#define KEY_REPEAT_PERIOD_MS 150  // 之后每次重复的周期

// 按键状态
typedef struct {
    button_t last_key;
    bool pressed;
    lv_point_t point;  // 用于模拟触摸位置（可选）
    uint32_t press_time_ms;       // 按键首次按下的时间
    uint32_t last_repeat_time_ms; // 上次重复事件的时间
} button_state_t;

static button_state_t btn_state = {
    .last_key = BTN_NONE,
    .pressed = false,
    .point = {0, 0},
    .press_time_ms = 0,
    .last_repeat_time_ms = 0
};

// LVGL keypad expects the last key to be reported even on RELEASED.
// If key is cleared to 0 too early, some widgets/group navigation may not receive KEY events reliably.
static uint32_t s_last_lvgl_key = 0;

// 输入设备读取回调 - LVGL 9.x
static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    button_t btn = get_pressed_button();

    // 调试：定期输出按钮状态（仅在状态变化时）
    static button_t last_debug_btn = BTN_NONE;
    if (btn != last_debug_btn) {
        if (btn != BTN_NONE) {
            ESP_LOGI(TAG, "Button detected: %d", btn);
        }
        last_debug_btn = btn;
    }

    // 检测按键状态变化
    if (btn != BTN_NONE && btn != btn_state.last_key) {
        // 新按键按下
        btn_state.pressed = true;
        btn_state.last_key = btn;
        btn_state.press_time_ms = 0;        // 初始化计时器
        btn_state.last_repeat_time_ms = 0;

        // 根据按键发送不同的输入事件
        // 按键映射方案：
        // - BTN_RIGHT (1)     -> LV_KEY_RIGHT     (右键)
        // - BTN_LEFT (2)      -> LV_KEY_LEFT      (左键)
        // - BTN_CONFIRM (3)   -> LV_KEY_ENTER     (确认)
        // - BTN_BACK (4)      -> LV_KEY_ESC       (返回)
        // - BTN_VOLUME_UP (5) -> LV_KEY_UP        (上)
        // - BTN_VOLUME_DOWN (6)-> LV_KEY_DOWN      (下)
        // - BTN_POWER (7)     -> (处理长按/短按，不发送到LVGL)
        switch (btn) {
            case BTN_CONFIRM:
                data->key = LV_KEY_ENTER;
                break;
            case BTN_BACK:
                data->key = LV_KEY_ESC;
                break;
            case BTN_LEFT:
                data->key = LV_KEY_LEFT;
                break;
            case BTN_RIGHT:
                data->key = LV_KEY_RIGHT;
                break;
            case BTN_VOLUME_UP:
                data->key = LV_KEY_UP;
                break;
            case BTN_VOLUME_DOWN:
                data->key = LV_KEY_DOWN;
                break;
            case BTN_POWER:
                // 电源按钮不发送LVGL按键，但记录按下时间用于长按检测
                data->key = 0;
                ESP_LOGI(TAG, "Power button pressed");
                break;
            default:
                data->key = 0;
                break;
        }

        data->state = LV_INDEV_STATE_PRESSED;
        s_last_lvgl_key = data->key;
        ESP_LOGI(TAG, "Key pressed: btn=%d -> LVGL key=%d", btn, (int)data->key);
    } else if (btn == BTN_NONE && btn_state.pressed) {
        // 按键释放
        btn_state.pressed = false;
        btn_state.last_key = BTN_NONE;
        btn_state.press_time_ms = 0;        // 重置计时器
        btn_state.last_repeat_time_ms = 0;  // 重置计时器
        data->state = LV_INDEV_STATE_RELEASED;
        // Keep reporting the last key on release.
        data->key = s_last_lvgl_key;
        ESP_LOGI(TAG, "Key released");
    } else if (btn_state.pressed && btn_state.last_key != BTN_NONE) {
        // 按键持续按下 - 带防抖延迟的重复事件
        uint32_t now = lv_tick_get();
        bool should_repeat = false;

        if (btn_state.press_time_ms == 0) {
            // 首次按下，初始化计时器
            btn_state.press_time_ms = now;
            btn_state.last_repeat_time_ms = now;
            should_repeat = true;
        } else if ((now - btn_state.last_repeat_time_ms) >= KEY_REPEAT_PERIOD_MS &&
                   (now - btn_state.press_time_ms) >= KEY_REPEAT_DELAY_MS) {
            // 已超过首次延迟，且到达重复周期
            btn_state.last_repeat_time_ms = now;
            should_repeat = true;
        }

        if (should_repeat) {
            // 发送重复的KEY事件
            switch (btn_state.last_key) {
                case BTN_CONFIRM:
                    data->key = LV_KEY_ENTER;
                    break;
                case BTN_BACK:
                    data->key = LV_KEY_ESC;
                    break;
                case BTN_LEFT:
                    data->key = LV_KEY_LEFT;
                    break;
                case BTN_RIGHT:
                    data->key = LV_KEY_RIGHT;
                    break;
                case BTN_VOLUME_UP:
                    data->key = LV_KEY_UP;
                    break;
                case BTN_VOLUME_DOWN:
                    data->key = LV_KEY_DOWN;
                    break;
                default:
                    data->key = 0;
                    break;
            }
            data->state = LV_INDEV_STATE_PRESSED;
            s_last_lvgl_key = data->key;
            ESP_LOGD(TAG, "Key repeat: %d (press_duration=%ldms)", btn_state.last_key, now - btn_state.press_time_ms);
        } else {
            // 还未到重复时间，不发送事件
            data->state = LV_INDEV_STATE_RELEASED;
            data->key = 0;
        }
    } else {
        // 无按键
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = s_last_lvgl_key;
    }
}

// 初始化LVGL输入设备驱动 - LVGL 9.x
lv_indev_t* lvgl_input_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL input driver (LVGL 9.x)");

    // 创建输入设备 - LVGL 9.x 新API
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, keypad_read_cb);

    ESP_LOGI(TAG, "LVGL input driver initialized");

    return indev;
}

// LVGL tick任务
void lvgl_tick_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_tick_inc(10);
    }
}

// LVGL定时器任务
void lvgl_timer_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL timer task started");

    while (1) {
        // 增加延迟到 30ms，避免占用过多 CPU 时间
        // EPD 不需要高频率刷新，30ms 已经足够
        vTaskDelay(pdMS_TO_TICKS(30));
        lv_timer_handler();
    }
}
