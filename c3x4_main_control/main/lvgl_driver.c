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
#include <stdlib.h>
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

static void dirty_area_add(const lv_area_t *area)
{
    if (!s_dirty_valid) {
        s_dirty_area = *area;
        s_dirty_valid = true;
        return;
    }

    if (area->x1 < s_dirty_area.x1) s_dirty_area.x1 = area->x1;
    if (area->y1 < s_dirty_area.y1) s_dirty_area.y1 = area->y1;
    if (area->x2 > s_dirty_area.x2) s_dirty_area.x2 = area->x2;
    if (area->y2 > s_dirty_area.y2) s_dirty_area.y2 = area->y2;
}

// 直接写入 1bpp framebuffer 的辅助函数
// 坐标映射：LVGL 逻辑坐标 (x, y) -> 物理屏幕坐标 (memX, memY) 使用 ROTATE_270
static inline void set_epd_pixel(int32_t x, int32_t y, bool black)
{
    if (x < 0 || x >= DISP_HOR_RES || y < 0 || y >= DISP_VER_RES) return;

    // ROTATE_270 映射: memX = y, memY = EPD_HEIGHT - 1 - x
    const int32_t memX = y;
    const int32_t memY = EPD_HEIGHT - 1 - x;

    const uint32_t byte_index = (uint32_t)memY * (EPD_WIDTH / 8) + (uint32_t)(memX / 8);
    const uint8_t bit_mask = 1 << (7 - (memX % 8));

    if (black) {
        s_epd_framebuffer[byte_index] |= bit_mask;  // Set bit = black
    } else {
        s_epd_framebuffer[byte_index] &= ~bit_mask; // Clear bit = white
    }
}

// LVGL 9.x 显示flush回调
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x, y;

    // ESP_LOGI(TAG, "Flushing area: x1=%d, y1=%d, x2=%d, y2=%d",
    //          area->x1, area->y1, area->x2, area->y2);

    // 获取显示颜色格式
    lv_color_format_t cf = lv_display_get_color_format(disp);

    // 获取缓冲区宽度（stride）
    int32_t buf_w = lv_area_get_width(area);

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

// 初始化LVGL显示驱动
lv_display_t* lvgl_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL display driver (LVGL 9.x)");

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

// 强制完整刷新EPD
void lvgl_display_refresh(void)
{
    if (s_dirty_valid) {
        // LVGL logical coords: 480x800 with ROTATE_270 mapping.
        // Physical mapping (ROTATE_270): memX = y, memY = (EPD_HEIGHT - 1 - x)
        int32_t mem_x1 = s_dirty_area.y1;
        int32_t mem_x2 = s_dirty_area.y2;
        int32_t mem_y1 = (EPD_HEIGHT - 1) - s_dirty_area.x2;
        int32_t mem_y2 = (EPD_HEIGHT - 1) - s_dirty_area.x1;
        if (mem_y1 > mem_y2) {
            const int32_t tmp = mem_y1;
            mem_y1 = mem_y2;
            mem_y2 = tmp;
        }

        // Align to 8 pixels along the physical X axis for safe 1bpp conversion.
        mem_x1 = mem_x1 & ~0x7;
        mem_x2 = (mem_x2 + 7) & ~0x7;

        if (mem_x1 < 0) mem_x1 = 0;
        if (mem_y1 < 0) mem_y1 = 0;
        if (mem_x2 >= EPD_WIDTH) mem_x2 = EPD_WIDTH - 1;
        if (mem_y2 >= EPD_HEIGHT) mem_y2 = EPD_HEIGHT - 1;

        const uint32_t mem_w = (uint32_t)(mem_x2 - mem_x1 + 1);
        const uint32_t mem_h = (uint32_t)(mem_y2 - mem_y1 + 1);

        // If too large, fall back to full BW refresh.
        const uint32_t screen_area = (uint32_t)EPD_WIDTH * (uint32_t)EPD_HEIGHT;
        const uint32_t dirty_area = mem_w * mem_h;

        ESP_LOGI(TAG, "EPD refresh: dirty_area=%u (%.1f%% of screen)",
                 (unsigned)dirty_area, (float)dirty_area * 100.0f / (float)screen_area);

        if (dirty_area > (screen_area * 3) / 5) {
            // 提高阈值到 60%，只有更大的区域才使用全刷
            ESP_LOGI(TAG, "Using full refresh");
            EPD_4in26_Display(s_epd_framebuffer);
        } else {
            // BW partial: crop the 1bpp framebuffer window into a contiguous buffer
            // and use the driver's BW partial update API.
            const UWORD x0 = (UWORD)mem_x1;
            const UWORD y0 = (UWORD)mem_y1;
            const UWORD w0 = (UWORD)mem_w;
            const UWORD h0 = (UWORD)mem_h;

            const UWORD out_stride = (UWORD)((w0 + 7) / 8);
            const uint32_t out_size = (uint32_t)out_stride * (uint32_t)h0;
            UBYTE *bw = (UBYTE *)malloc(out_size);
            if (!bw) {
                ESP_LOGW(TAG, "BW partial buffer alloc failed (%u bytes); falling back to full refresh", (unsigned)out_size);
                EPD_4in26_Display(s_epd_framebuffer);
            } else {
                const uint32_t in_stride = (uint32_t)(EPD_WIDTH / 8);
                const uint32_t in_x_byte = (uint32_t)(x0 / 8);

                for (UWORD row = 0; row < h0; row++) {
                    const uint32_t in_base = (uint32_t)(y0 + row) * in_stride + in_x_byte;
                    memcpy(bw + (uint32_t)row * out_stride, s_epd_framebuffer + in_base, out_stride);
                }

                ESP_LOGI(TAG, "BW partial refresh: x=%u y=%u w=%u h=%u area=%u",
                         (unsigned)x0, (unsigned)y0, (unsigned)w0, (unsigned)h0, (unsigned)dirty_area);
                EPD_4in26_Display_Part(bw, x0, y0, w0, h0);
                free(bw);
            }
        }

        s_dirty_valid = false;
    } else {
        // No dirty area recorded; do a full refresh.
        ESP_LOGI(TAG, "EPD refresh: no dirty area, using full refresh");
        EPD_4in26_Display(s_epd_framebuffer);
    }

    // 不再阻塞等待，让 EPD 异步刷新
    // EPD 刷新大约需要 2 秒，但我们不需要等待
    ESP_LOGI(TAG, "EPD refresh started (async)");
}

/* ========================================================================
 * 输入设备驱动 - 按键 (LVGL 9.x)
 * ========================================================================*/

// 按键状态
typedef struct {
    button_t last_key;
    bool pressed;
    lv_point_t point;  // 用于模拟触摸位置（可选）
} button_state_t;

static button_state_t btn_state = {
    .last_key = BTN_NONE,
    .pressed = false,
    .point = {0, 0}
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

        // 根据按键发送不同的输入事件
        // 按键映射方案：
        // - BTN_RIGHT (1)     -> LV_KEY_RIGHT     (右键)
        // - BTN_LEFT (2)      -> LV_KEY_LEFT      (左键)
        // - BTN_CONFIRM (3)   -> LV_KEY_ENTER     (确认)
        // - BTN_BACK (4)      -> LV_KEY_ESC       (返回)
        // - BTN_VOLUME_UP (5) -> LV_KEY_UP        (上)
        // - BTN_VOLUME_DOWN (6)-> LV_KEY_DOWN      (下)
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
        data->state = LV_INDEV_STATE_RELEASED;
        // Keep reporting the last key on release.
        data->key = s_last_lvgl_key;
        ESP_LOGI(TAG, "Key released");
    } else if (btn_state.pressed && btn_state.last_key != BTN_NONE) {
        // 按键持续按下 - 保持发送相同的 key
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
