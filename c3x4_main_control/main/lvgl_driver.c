/**
 * @file lvgl_driver.c
 * @brief LVGL驱动适配层 - EPD和按键输入
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
static lv_disp_draw_buf_t disp_buf;
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

// 显示刷新回调函数
static void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t x, y;

    ESP_LOGI(TAG, "Flushing area: x1=%d, y1=%d, x2=%d, y2=%d",
             area->x1, area->y1, area->x2, area->y2);

    // 将 LVGL 的缓冲区数据直接写入到 1bpp framebuffer。
    // 坐标为 LVGL 逻辑坐标（竖屏 480x800）。
    // 使用 set_epd_pixel 直接操作 framebuffer，无需 Paint 层。
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            // RGB565 -> 亮度 (0..255)
            // 说明：文字抗锯齿会产生灰度；黑白模式下做阈值化处理。
            const uint16_t c = (uint16_t)color_p->full;
            const uint8_t r5 = (c >> 11) & 0x1F;
            const uint8_t g6 = (c >> 5) & 0x3F;
            const uint8_t b5 = (c >> 0) & 0x1F;
            const uint8_t r8 = (uint8_t)((r5 * 255) / 31);
            const uint8_t g8 = (uint8_t)((g6 * 255) / 63);
            const uint8_t b8 = (uint8_t)((b5 * 255) / 31);
            const uint16_t lum = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11); // 0..25500
            const uint8_t y8 = (uint8_t)(lum / 100);

            // 阈值：亮->白，暗->黑
            // 经验值：128 在多数 UI 上对比更清晰；如需更黑可降到 150~170。
            const bool black = (y8 < 128);
            set_epd_pixel(x, y, black);
            color_p++;
        }
    }

    // Record dirty area for partial EPD refresh later.
    dirty_area_add(area);

    // 通知LVGL刷新完成
    lv_disp_flush_ready(disp_drv);
}

// 可选：Rounder回调 - 对于某些EPD可能需要对齐到8像素边界
static void disp_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    // EPD通常需要8像素对齐（1字节 = 8像素）
    area->x1 = area->x1 & ~0x7;
    area->x2 = (area->x2 + 7) & ~0x7;

    // rounder 可能把 x2 从 799 对齐到 800，导致 flush 循环写越界
    if (area->x1 < 0) area->x1 = 0;
    if (area->y1 < 0) area->y1 = 0;
    if (area->x2 >= DISP_HOR_RES) area->x2 = DISP_HOR_RES - 1;
    if (area->y2 >= DISP_VER_RES) area->y2 = DISP_VER_RES - 1;
}

// 初始化LVGL显示驱动
lv_disp_t* lvgl_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL display driver");
    
    // 初始化LVGL
    lv_init();
    
    // 初始化绘图缓冲区
    // 单缓冲：省 RAM（对 ESP32-C3 很关键）
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, DISP_HOR_RES * DISP_BUF_LINES);
    
    // 初始化显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.rounder_cb = disp_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    
    // Enable partial rendering (dirty rectangles). We'll still do EPD refresh on demand.
    disp_drv.full_refresh = 0;
    
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    
    ESP_LOGI(TAG, "LVGL display driver initialized (logical): %dx%d", 
             DISP_HOR_RES, DISP_VER_RES);
    
    return disp;
}

// 强制完整刷新EPD
void lvgl_display_refresh(void)
{
    ESP_LOGI(TAG, "Refreshing EPD display");

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
        if (dirty_area > (screen_area * 2) / 5) {
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

                ESP_LOGI(TAG, "BW partial refresh: x=%u y=%u w=%u h=%u", (unsigned)x0, (unsigned)y0, (unsigned)w0, (unsigned)h0);
                EPD_4in26_Display_Part(bw, x0, y0, w0, h0);
                free(bw);
            }
        }

        s_dirty_valid = false;
    } else {
        // No dirty area recorded; do a full refresh.
        EPD_4in26_Display(s_epd_framebuffer);
    }

    // 不再阻塞等待，让 EPD 异步刷新
    // EPD 刷新大约需要 2 秒，但我们不需要等待
    ESP_LOGI(TAG, "EPD refresh started (async)");
}

/* ========================================================================
 * 输入设备驱动 - 按键
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

// 输入设备读取回调
static void keypad_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
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
                // LVGL group navigation expects PREV/NEXT rather than UP/DOWN
                data->key = LV_KEY_PREV;
                break;
            case BTN_VOLUME_DOWN:
                // LVGL group navigation expects PREV/NEXT rather than UP/DOWN
                data->key = LV_KEY_NEXT;
                break;
            default:
                data->key = 0;
                break;
        }

        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "Key pressed: %d -> LVGL key: %d", btn, (int)data->key);
    } else if (btn == BTN_NONE && btn_state.pressed) {
        // 按键释放
        btn_state.pressed = false;
        btn_state.last_key = BTN_NONE;
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0;
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
                data->key = LV_KEY_PREV;
                break;
            case BTN_VOLUME_DOWN:
                data->key = LV_KEY_NEXT;
                break;
            default:
                data->key = 0;
                break;
        }
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        // 无按键
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0;
    }

    data->continue_reading = false;
}

// 初始化LVGL输入设备驱动
lv_indev_t* lvgl_input_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL input driver");
    
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keypad_read_cb;
    
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
    
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
