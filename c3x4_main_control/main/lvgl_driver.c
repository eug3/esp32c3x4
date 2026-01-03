/**
 * @file lvgl_driver.c
 * @brief LVGL驱动适配层 - EPD和按键输入 (LVGL 9.x)
 */

#include "lvgl_driver.h"
#include "EPD_4in26.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
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
#define MAX_PARTIAL_REFRESHES  10

// 显示缓冲区
static lv_color_t buf1[DISP_HOR_RES * DISP_BUF_LINES];

// 1bpp framebuffer for EPD (directly operated by LVGL)
// 使用静态分配，避免依赖外部的 BlackImage
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];

// Track the union of flushed areas since last EPD refresh.
static lv_area_t s_dirty_area;
static bool s_dirty_valid = false;

// 当前刷新模式(可动态切换)
// 重要: 首次刷新应用FULL模式,确保framebuffer与EPD完全同步!
// 之后可切换为FAST或PARTIAL提升速度
static epd_refresh_mode_t s_refresh_mode = EPD_REFRESH_FULL;

// 局部刷新计数器: 多次局部刷新后强制全刷以消除鬼影
static uint32_t s_partial_refresh_count = 0;
#define FORCE_FULL_REFRESH_AFTER_N_PARTIAL 10

// EPD refresh state tracking (异步刷新)
static bool s_epd_refreshing = false;
static bool s_render_done = false;           // disp_flush_cb 已完成写入
static SemaphoreHandle_t s_epd_mutex = NULL;
static SemaphoreHandle_t s_render_done_sem = NULL;  // 渲染完成信号
static TaskHandle_t s_epd_refresh_task_handle = NULL;
static QueueHandle_t s_refresh_queue = NULL;

// Protect dirty area state against concurrent access (flush_cb vs refresh task).
static portMUX_TYPE s_dirty_mux = portMUX_INITIALIZER_UNLOCKED;

// 全局显示设备指针（用于手动刷新模式）
static lv_display_t *g_lv_display = NULL;

// 刷新请求结构
typedef struct {
    epd_refresh_mode_t mode;  // 刷新模式
    void (*on_complete)(void);  // 刷新完成回调（可选）
} refresh_request_t;

// 刷新完成回调（用于恢复焦点等）
static void (*s_refresh_complete_callback)(void) = NULL;

// 注册刷新完成回调
void lvgl_register_refresh_complete_callback(void (*callback)(void))
{
    s_refresh_complete_callback = callback;
}

// 手动触发 LVGL 渲染刷新（用于 EPD 手动刷新模式）
void lvgl_trigger_render(lv_display_t *disp)
{
    // 如果传入 NULL，使用全局 display
    if (disp == NULL) {
        disp = g_lv_display;
    }

    if (disp != NULL) {
        // Avoid rendering while EPD refresh task is reading/sending framebuffer.
        // This prevents mixed old/new bytes being sent to the panel.
        const TickType_t start = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS(8000);
        while (s_epd_refreshing) {
            if ((xTaskGetTickCount() - start) > timeout) {
                ESP_LOGW(TAG, "lvgl_trigger_render: timed out waiting for EPD refresh");
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 调用一次 lv_timer_handler 处理动画和定时器
        lv_timer_handler();
        // 立即触发渲染
        lv_refr_now(disp);
    } else {
        ESP_LOGW(TAG, "lvgl_trigger_render: display is NULL!");
    }
}

// 优化：不再使用单独的局部刷新缓冲区
// 改为流式发送，直接从 s_epd_framebuffer 按行发送数据
// 节省内存：约 12 KB

static void dirty_area_add(const lv_area_t *area)
{
    // NOTE: Never call ESP_LOG inside a critical section; logging may acquire locks.
    bool do_expand_log = false;
    int32_t y_gap = 0;
    lv_area_t init_area = {0};
    lv_area_t old_area = {0};
    lv_area_t new_area = {0};

    portENTER_CRITICAL(&s_dirty_mux);

    if (!s_dirty_valid) {
        s_dirty_area = *area;
        s_dirty_valid = true;
        init_area = *area;
        portEXIT_CRITICAL(&s_dirty_mux);

        ESP_LOGI(TAG, "[DIRTY] init: (%d,%d)-(%d,%d)",
                 (int)init_area.x1, (int)init_area.y1, (int)init_area.x2, (int)init_area.y2);
        return;
    }

    old_area = s_dirty_area;

    // 计算新旧区域之间的距离（基于 old_area）
    y_gap = (area->y1 > old_area.y2) ? (area->y1 - old_area.y2 - 1)
            : ((old_area.y1 > area->y2) ? (old_area.y1 - area->y2 - 1)
            : 0);

    // 总是扩展脏区到包含所有已刷新区域的包围盒
    if (area->x1 < s_dirty_area.x1) s_dirty_area.x1 = area->x1;
    if (area->y1 < s_dirty_area.y1) s_dirty_area.y1 = area->y1;
    if (area->x2 > s_dirty_area.x2) s_dirty_area.x2 = area->x2;
    if (area->y2 > s_dirty_area.y2) s_dirty_area.y2 = area->y2;

    new_area = s_dirty_area;
    if (old_area.x1 != new_area.x1 || old_area.y1 != new_area.y1 ||
        old_area.x2 != new_area.x2 || old_area.y2 != new_area.y2) {
        do_expand_log = true;
    }

    portEXIT_CRITICAL(&s_dirty_mux);

    if (do_expand_log) {
        ESP_LOGI(TAG, "[DIRTY] expanded: (%d,%d)-(%d,%d) -> (%d,%d)-(%d,%d), y_gap=%d",
                 (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                 (int)new_area.x1, (int)new_area.y1,
                 (int)new_area.x2, (int)new_area.y2, (int)y_gap);
    }
}

static void queue_refresh_request(epd_refresh_mode_t mode)
{
    if (s_refresh_queue == NULL) {
        ESP_LOGW(TAG, "Refresh queue not initialized");
        return;
    }

    refresh_request_t req = {
        .mode = mode,
        .on_complete = NULL,
    };

    // Keep only the latest request to avoid missing updates when UI triggers refresh rapidly.
    // Special-case: once a FULL refresh is queued (e.g. screen switch), do not let
    // subsequent PARTIAL/FAST requests overwrite it.
    // Note: xQueueOverwrite requires queue length == 1.
    refresh_request_t queued;
    if (xQueuePeek(s_refresh_queue, &queued, 0) == pdTRUE) {
        if (queued.mode == EPD_REFRESH_FULL && mode != EPD_REFRESH_FULL) {
            return;
        }
    }

    (void)xQueueOverwrite(s_refresh_queue, &req);
}

// LVGL 9.x 显示flush回调
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // 获取互斥锁，保护 framebuffer 访问
    // 如果获取失败，可能是刷新任务正在读取，跳过这次写入
    if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "disp_flush_cb: failed to acquire mutex, skipping write");
        lv_display_flush_ready(disp);
        return;
    }

    // 标记渲染已开始（用于刷新任务判断）
    s_render_done = false;

    // 记录每次 flush 调用
    static uint32_t flush_count = 0;
    flush_count++;

    int32_t x, y;

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

    // 首次 flush 时打印信息
    if (flush_count <= 20) {
        ESP_LOGI(TAG, "disp_flush_cb #%u: area(%d,%d)-(%d,%d), cf=%d",
                 flush_count, (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2, (int)cf);
    }

    // ============================================================
    // LVGL 渲染说明：
    // LVGL 会全屏渲染所有需要更新的区域，不管我们使用何种刷新模式。
    // - PARTIAL 模式：LVGL 全屏渲染 -> EPD 只刷新脏区（减少硬件通信）
    // - FAST 模式：LVGL 全屏渲染 -> EPD 全屏快刷
    // - FULL 模式：LVGL 全屏渲染 -> EPD 全屏全刷
    //
    // 关键点：我们不需要在 flush_cb 中裁剪渲染区域，因为：
    // 1. LVGL 已经计算并渲染了整个 area 中的所有像素
    // 2. 裁剪只会跳过 framebuffer 的写入，但 LVGL 已经做了渲染工作
    // 3. 真正的优化在于 EPD 刷新时只发送脏区数据给硬件（在 epd_refresh_task 中实现）
    // ============================================================

    // 开始处理flush
    uint32_t black_count = 0;
    uint32_t white_count = 0;

    // 将 LVGL 的缓冲区数据直接写入到 1bpp framebuffer（坐标为 LVGL 逻辑坐标）

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
            } else {
                // 未知的颜色格式
                ESP_LOGE(TAG, "[CF_ERROR] Unknown color format: cf=%d at LVGL(%d,%d)", (int)cf, (int)x, (int)y);
                continue; // 跳过这个像素
            }

            if (black) {
                black_count++;
            } else {
                white_count++;
            }

            // ========== 直接写入 framebuffer（内联版本） ==========
            // ROTATE_270 映射: LVGL(x,y) -> EPD(memX,memY)
            // memX = y, memY = EPD_HEIGHT - 1 - x
            const int32_t memX = y;
            const int32_t memY = EPD_HEIGHT - 1 - x;

            const uint32_t byte_index = (uint32_t)memY * (EPD_WIDTH / 8) + (uint32_t)(memX / 8);
            const uint8_t bit_mask = 1 << (7 - (memX % 8));

            if (byte_index < sizeof(s_epd_framebuffer)) {
                if (black) {
                    s_epd_framebuffer[byte_index] &= ~bit_mask; // Clear bit = black
                } else {
                    s_epd_framebuffer[byte_index] |= bit_mask;  // Set bit = white
                }
            } else {
                ESP_LOGE(TAG, "FB overflow at idx=%u", (unsigned)byte_index);
            }
            // ===================================================
        }
    }

    // Flush处理完成
    if (flush_count <= 20) {
        ESP_LOGI(TAG, "disp_flush_cb #%u: area(%d,%d)-(%d,%d), black=%u, white=%u, total=%u",
                 flush_count, (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                 black_count, white_count, black_count + white_count);
    }

    // ============================================================
    // 脏区跟踪说明：
    // 只在 PARTIAL 模式下记录脏区域，用于后续 EPD 刷新时裁剪发送的数据。
    //
    // 注意：这里的脏区跟踪只是记录 LVGL 渲染过的区域，
    //     用于告知 EPD 硬件哪些区域的数据发生了变化。
    // LVGL 本身仍然会渲染所有需要更新的控件（全屏渲染）。
    //
    // FULL 和 FAST 模式会发送完整 framebuffer 给 EPD，不需要脏区域跟踪。
    // ============================================================
    if (s_refresh_mode == EPD_REFRESH_PARTIAL) {
        dirty_area_add(area);
    }

    // 标记渲染完成（用于调试）
    s_render_done = true;

    // 释放互斥锁
    xSemaphoreGive(s_epd_mutex);

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

    // 创建信号量（用于 disp_flush_cb 通知渲染完成）
    if (s_render_done_sem == NULL) {
        s_render_done_sem = xSemaphoreCreateBinary();
        if (s_render_done_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create render done semaphore!");
            return NULL;
        }
    }

    // 创建刷新请求队列（异步刷新）
    if (s_refresh_queue == NULL) {
        // Single-slot queue + overwrite ensures the latest refresh request is not dropped.
        s_refresh_queue = xQueueCreate(1, sizeof(refresh_request_t));
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
    
    // 测试：手动写入几个黑色像素到framebuffer
    // 尝试在 (0,0) 位置写入黑色
    s_epd_framebuffer[0] = 0xFE; // 最高位清零 = 黑色像素
    ESP_LOGI(TAG, "[TEST] Manual write: s_epd_framebuffer[0] = 0x%02X", s_epd_framebuffer[0]);
    
    // 尝试在多个位置写入测试模式
    for (uint32_t i = 0; i < 10; i++) {
        s_epd_framebuffer[i * 100] = 0x00; // 每隔100字节写入全黑
    }
    ESP_LOGI(TAG, "[TEST] Manual black pixels written at indices 0, 100, 200, ..., 900");

    // 清空脏区域追踪
    s_dirty_valid = false;
    s_partial_refresh_count = 0;
    s_epd_refreshing = false;
    s_render_done = false;

    // 初始化LVGL
    lv_init();

    // 创建显示设备 - LVGL 9.x 新API
    lv_display_t *disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush_cb);

    // 保存 display 指针到全局变量（用于手动刷新模式）
    g_lv_display = disp;

    // EPD手动刷新模式: 
    // - 渲染模式DIRECT: 禁用自动刷新,手动控制何时更新屏幕
    // - 缓冲区模式PARTIAL: 使用小缓冲区逐块渲染(因为RAM有限)
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);

    // 设置缓冲区 - 必须用PARTIAL模式,因为缓冲区远小于整个屏幕
    // buf1大小: 480×5×2 = 4800字节,而全屏需要750KB
    uint32_t buf_size = DISP_HOR_RES * DISP_BUF_LINES * sizeof(lv_color_t);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL display driver initialized (logical): %dx%d (manual refresh mode)",
             DISP_HOR_RES, DISP_VER_RES);

    return disp;
}

// 异步 EPD 刷新任务
static void epd_refresh_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "EPD refresh task started");

    while (1) {
        refresh_request_t req;
        // 等待刷新请求，阻塞等待
        if (xQueueReceive(s_refresh_queue, &req, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "EPD refresh task: received request, mode=%d", req.mode);

            // Mark refreshing to indicate EPD is busy
            s_epd_refreshing = true;
            __sync_synchronize();

            // 等待 disp_flush_cb 完成（不持有锁）
            // 如果超时或 s_render_done 已经为 true，直接继续
            const TickType_t wait_start = xTaskGetTickCount();
            bool waited_for_render = false;
            while (!s_render_done) {
                if ((xTaskGetTickCount() - wait_start) > pdMS_TO_TICKS(200)) {
                    // 等待超过 200ms，不再等待，直接发送当前数据
                    ESP_LOGW(TAG, "EPD refresh task: render not ready, sending current data");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                waited_for_render = true;
            }
            if (waited_for_render) {
                ESP_LOGI(TAG, "EPD refresh task: render completed, sending updated data");
            }

            // 再获取锁执行刷新
            if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                ESP_LOGI(TAG, "EPD refresh task: refreshing, s_epd_refreshing=%d", s_epd_refreshing);

                epd_refresh_mode_t mode = req.mode;

                // ============================================================
                // 刷新模式选择逻辑：
                // 1. FULL: 执行全刷（用于屏幕切换，确保显示清晰）
                // 2. FAST: 执行快刷（全屏数据，速度快）
                // 3. PARTIAL:
                //    - 如果局刷计数 = 0：执行快刷（第一次或重置后）
                //    - 否则：执行局刷，计数器+1
                //    - 如果计数器 >= 10：重置计数器，下次将执行快刷
                // ============================================================

                if (mode == EPD_REFRESH_FULL) {
                    ESP_LOGI(TAG, "EPD refresh task: FULL refresh (requested)");
                    EPD_4in26_Display(s_epd_framebuffer);
                    s_partial_refresh_count = 0;
                    portENTER_CRITICAL(&s_dirty_mux);
                    s_dirty_valid = false;
                    portEXIT_CRITICAL(&s_dirty_mux);

                } else if (mode == EPD_REFRESH_FAST) {
                    ESP_LOGI(TAG, "EPD refresh task: FAST refresh");
                    EPD_4in26_Display_Fast(s_epd_framebuffer);
                    s_partial_refresh_count = 0;
                    portENTER_CRITICAL(&s_dirty_mux);
                    s_dirty_valid = false;
                    portEXIT_CRITICAL(&s_dirty_mux);

                } else if (mode == EPD_REFRESH_PARTIAL) {
                    // 局刷次数为 0 时，直接执行快刷
                    if (s_partial_refresh_count == 0) {
                        ESP_LOGI(TAG, "EPD refresh task: PARTIAL count=0, using FAST refresh");
                        EPD_4in26_Display_Fast(s_epd_framebuffer);
                        portENTER_CRITICAL(&s_dirty_mux);
                        s_dirty_valid = false;
                        portEXIT_CRITICAL(&s_dirty_mux);
                        s_partial_refresh_count++;  // 从 1 开始计数
                        goto refresh_done;
                    }

                    // Snapshot dirty area atomically to avoid concurrent modification by flush_cb.
                    bool dirty_valid;
                    lv_area_t dirty_area;
                    portENTER_CRITICAL(&s_dirty_mux);
                    dirty_valid = s_dirty_valid;
                    dirty_area = s_dirty_area;
                    portEXIT_CRITICAL(&s_dirty_mux);

                    if (!dirty_valid) {
                        ESP_LOGW(TAG, "EPD refresh task: PARTIAL requested but no dirty area, skipping");
                        goto partial_done;
                    }

                    // ============================================================
                    // 局部刷新优化方案：
                    // 直接使用完整的 s_epd_framebuffer，只裁剪脏区发送给硬件
                    // ============================================================

                    // 获取脏区尺寸
                    const int32_t dirty_w = dirty_area.x2 - dirty_area.x1 + 1;
                    const int32_t dirty_h = dirty_area.y2 - dirty_area.y1 + 1;

                    // LVGL 坐标系 (480x800) 到 EPD 物理坐标系 (800x480) 的映射
                    // ROTATE_270: LVGL(x,y) -> EPD(memX=y, memY=EPD_HEIGHT-1-x)
                    int32_t epd_x = dirty_area.y1;
                    int32_t epd_y = (int32_t)EPD_HEIGHT - 1 - dirty_area.x2;
                    int32_t epd_w = dirty_h;  // 注意：宽高互换
                    int32_t epd_h = dirty_w;

                    // EPD 硬件要求：X 坐标必须是 8 的倍数（字节对齐）
                    if (epd_x % 8 != 0) {
                        const int32_t orig_x = epd_x;
                        epd_x = (epd_x / 8) * 8;  // 向下对齐
                        epd_w += (orig_x - epd_x);  // 补偿宽度
                    }
                    if (epd_w % 8 != 0) {
                        epd_w = ((epd_w + 7) / 8) * 8;  // 向上对齐
                    }

                    // 边界检查
                    if (epd_x < 0) epd_x = 0;
                    if (epd_y < 0) epd_y = 0;
                    if (epd_x + epd_w > EPD_WIDTH) epd_w = EPD_WIDTH - epd_x;
                    if (epd_y + epd_h > EPD_HEIGHT) epd_h = EPD_HEIGHT - epd_y;

                    ESP_LOGI(TAG, "EPD refresh task: PARTIAL #%u/%u LVGL(%d,%d,%dx%d) -> EPD(x=%d,y=%d,%dx%d)",
                             s_partial_refresh_count, FORCE_FULL_REFRESH_AFTER_N_PARTIAL,
                             (int)dirty_area.x1, (int)dirty_area.y1, (int)dirty_w, (int)dirty_h,
                             (int)epd_x, (int)epd_y, (int)epd_w, (int)epd_h);

                    // 从完整 framebuffer 裁剪脏区数据发送给硬件
                    if (epd_w > 0 && epd_h > 0) {
                        EPD_4in26_Display_Partial(s_epd_framebuffer,
                                                 (UWORD)epd_x, (UWORD)epd_y,
                                                 (UWORD)epd_w, (UWORD)epd_h);
                    } else {
                        ESP_LOGW(TAG, "EPD refresh task: invalid area, fallback FAST");
                        EPD_4in26_Display_Fast(s_epd_framebuffer);
                    }

                    // 计数器递增，达到阈值后重置
                    s_partial_refresh_count++;
                    if (s_partial_refresh_count >= FORCE_FULL_REFRESH_AFTER_N_PARTIAL) {
                        ESP_LOGI(TAG, "EPD refresh task: Reached %u PARTIALs, resetting count", s_partial_refresh_count);
                        s_partial_refresh_count = 0;
                    }

partial_done:
                    portENTER_CRITICAL(&s_dirty_mux);
                    s_dirty_valid = false;
                    portEXIT_CRITICAL(&s_dirty_mux);
                } else {
                    ESP_LOGE(TAG, "EPD refresh task: Unknown mode %d, fallback to FAST", mode);
                    EPD_4in26_Display_Fast(s_epd_framebuffer);
                    s_partial_refresh_count = 0;
                    s_dirty_valid = false;
                }

refresh_done:
                s_epd_refreshing = false;
                s_render_done = false;  // 重置，为下一次渲染做准备
                ESP_LOGI(TAG, "EPD refresh task: complete, s_epd_refreshing=%d", s_epd_refreshing);
                xSemaphoreGive(s_epd_mutex);

                // 调用刷新完成回调
                if (s_refresh_complete_callback != NULL) {
                    s_refresh_complete_callback();
                }
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for refresh");
                s_epd_refreshing = false;
                s_render_done = false;
            }
        }
    }
}

// 刷新 EPD - 使用当前配置的模式
void lvgl_display_refresh(void)
{
    queue_refresh_request(s_refresh_mode);
}

// 局部刷新 EPD
void lvgl_display_refresh_partial(void)
{
    queue_refresh_request(EPD_REFRESH_PARTIAL);
}

// 快速刷新 EPD
void lvgl_display_refresh_fast(void)
{
    queue_refresh_request(EPD_REFRESH_FAST);
}

// 全刷 EPD
void lvgl_display_refresh_full(void)
{
    queue_refresh_request(EPD_REFRESH_FULL);
}

// 设置刷新模式
void lvgl_set_refresh_mode(epd_refresh_mode_t mode)
{
    s_refresh_mode = mode;

    switch (mode) {
        case EPD_REFRESH_PARTIAL:
            ESP_LOGI(TAG, "Refresh mode set to PARTIAL (fastest, may have ghosting)");
            break;
        case EPD_REFRESH_FAST:
            ESP_LOGI(TAG, "Refresh mode set to FAST (balanced)");
            break;
        case EPD_REFRESH_FULL:
            ESP_LOGI(TAG, "Refresh mode set to FULL (clearest)");
            break;
    }
}

// 获取当前刷新模式
epd_refresh_mode_t lvgl_get_refresh_mode(void)
{
    return s_refresh_mode;
}

// 检查 EPD 是否正在刷新
bool lvgl_is_refreshing(void)
{
    return s_epd_refreshing;
}

// 重置刷新状态
void lvgl_reset_refresh_state(void)
{
    portENTER_CRITICAL(&s_dirty_mux);
    s_dirty_valid = false;
    portEXIT_CRITICAL(&s_dirty_mux);
    s_partial_refresh_count = 0;

    // 清空刷新队列，丢弃所有待处理的刷新请求
    // 这对于屏幕切换很重要：旧屏幕的 PARTIAL 刷新请求不应该污染新屏幕
    if (s_refresh_queue != NULL) {
        xQueueReset(s_refresh_queue);
        ESP_LOGI(TAG, "Cleared refresh queue during reset");
    }

    ESP_LOGI(TAG, "Refresh state reset (dirty_valid=%d, partial_count=%u)",
             s_dirty_valid, s_partial_refresh_count);
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

    // 读取按键状态

    // 检测按键状态变化
    if (btn != BTN_NONE && btn != btn_state.last_key) {
        // 新按键按下
        btn_state.pressed = true;
        btn_state.last_key = btn;
        btn_state.press_time_ms = 0;        // 初始化计时器
        btn_state.last_repeat_time_ms = 0;

        ESP_LOGI(TAG, "Key pressed: btn=%d", btn);

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
                ESP_LOGI(TAG, "Mapped to LV_KEY_ENTER");
                break;
            case BTN_BACK:
                data->key = LV_KEY_ESC;
                ESP_LOGI(TAG, "Mapped to LV_KEY_ESC");
                break;
            case BTN_LEFT:
                data->key = LV_KEY_LEFT;
                ESP_LOGI(TAG, "Mapped to LV_KEY_LEFT");
                break;
            case BTN_RIGHT:
                data->key = LV_KEY_RIGHT;
                ESP_LOGI(TAG, "Mapped to LV_KEY_RIGHT");
                break;
            case BTN_VOLUME_UP:
                data->key = LV_KEY_PREV;  // UP -> PREV (上一个)
                ESP_LOGI(TAG, "Mapped to LV_KEY_PREV (was UP)");
                break;
            case BTN_VOLUME_DOWN:
                data->key = LV_KEY_NEXT;  // DOWN -> NEXT (下一个)
                ESP_LOGI(TAG, "Mapped to LV_KEY_NEXT (was DOWN)");
                break;
            case BTN_POWER:
                // 电源按钮不发送LVGL按键，但记录按下时间用于长按检测
                data->key = 0;
                ESP_LOGI(TAG, "Power button pressed");
                break;
            default:
                data->key = 0;
                ESP_LOGI(TAG, "Unknown button, mapped to 0");
                break;
        }

        data->state = LV_INDEV_STATE_PRESSED;
        s_last_lvgl_key = data->key;
        ESP_LOGI(TAG, "Keypad event: key=%u, state=PRESSED", data->key);
    } else if (btn == BTN_NONE && btn_state.pressed) {
        // 按键释放
        btn_state.pressed = false;
        btn_state.last_key = BTN_NONE;
        btn_state.press_time_ms = 0;        // 重置计时器
        btn_state.last_repeat_time_ms = 0;  // 重置计时器
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = s_last_lvgl_key;
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

    ESP_LOGI(TAG, "LVGL input driver initialized (UP/DOWN mapped to PREV/NEXT for lv_group)");

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

// LVGL定时器任务 - 手动刷新模式
// 对于 EPD，我们仍然需要定期调用 lv_timer_handler() 来处理输入事件
// 但渲染是手动的，由 lv_refr_now() 触发
void lvgl_timer_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL timer task started (manual refresh mode for EPD)");

    while (1) {
        // 定期调用 lv_timer_handler() 处理：
        // 1. 输入设备读取和事件分发
        // 2. 动画和定时器
        // 3. 焦点管理
        // 但不会自动触发渲染（需要调用 lv_refr_now()）
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 周期，足够响应按键
    }
}
