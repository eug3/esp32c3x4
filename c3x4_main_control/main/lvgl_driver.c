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
#include <string.h>

static const char *TAG = "LVGL_DRV";

// EPD显示尺寸
#define EPD_WIDTH  800
#define EPD_HEIGHT 480
#define DISP_BUF_LINES 10

// 显示缓冲区
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf1[EPD_WIDTH * DISP_BUF_LINES];
static lv_color_t buf2[EPD_WIDTH * DISP_BUF_LINES];

// EPD图像缓冲区（用于GUI_Paint）
extern UBYTE *BlackImage;

// 显示刷新回调函数
static void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t x, y;
    
    ESP_LOGI(TAG, "Flushing area: x1=%d, y1=%d, x2=%d, y2=%d", 
             area->x1, area->y1, area->x2, area->y2);
    
    // 将LVGL的缓冲区数据写入到EPD的图像缓冲区
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            // LVGL 1-bit颜色：0=黑, 1=白
            // EPD: 0xFF=白, 0x00=黑（或相反，取决于驱动）
            lv_color_t color = *color_p;
            
            // 将像素写入Paint缓冲区
            if (color.full) {
                Paint_SetPixel(x, y, WHITE);
            } else {
                Paint_SetPixel(x, y, BLACK);
            }
            color_p++;
        }
    }
    
    // 通知LVGL刷新完成
    lv_disp_flush_ready(disp_drv);
}

// 可选：Rounder回调 - 对于某些EPD可能需要对齐到8像素边界
static void disp_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    // EPD通常需要8像素对齐（1字节 = 8像素）
    area->x1 = area->x1 & ~0x7;
    area->x2 = (area->x2 + 7) & ~0x7;
}

// 初始化LVGL显示驱动
lv_disp_t* lvgl_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL display driver");
    
    // 初始化LVGL
    lv_init();
    
    // 初始化绘图缓冲区
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EPD_WIDTH * DISP_BUF_LINES);
    
    // 初始化显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    disp_drv.hor_res = EPD_WIDTH;
    disp_drv.ver_res = EPD_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.rounder_cb = disp_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    
    // 对于EPD，可以使用完整刷新模式
    disp_drv.full_refresh = 1;
    
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    
    ESP_LOGI(TAG, "LVGL display driver initialized: %dx%d", 
             EPD_WIDTH, EPD_HEIGHT);
    
    return disp;
}

// 强制完整刷新EPD
void lvgl_display_refresh(void)
{
    ESP_LOGI(TAG, "Refreshing EPD display");
    
    // 发送图像到EPD
    EPD_4in26_Display(BlackImage);
    
    // 可选：等待刷新完成
    vTaskDelay(pdMS_TO_TICKS(100));
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
        ESP_LOGI(TAG, "Key pressed: %d", btn);
    } else if (btn == BTN_NONE && btn_state.pressed) {
        btn_state.pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
        ESP_LOGI(TAG, "Key released");
    } else {
        data->state = btn_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_timer_handler();
    }
}
