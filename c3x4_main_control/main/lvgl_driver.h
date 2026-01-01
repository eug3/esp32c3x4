/**
 * @file lvgl_driver.h
 * @brief LVGL驱动适配层头文件
 */

#ifndef LVGL_DRIVER_H
#define LVGL_DRIVER_H

#include "lvgl.h"

// 按钮枚举定义（从main.c复制）
typedef enum {
    BTN_NONE = 0,
    BTN_RIGHT,
    BTN_LEFT,
    BTN_CONFIRM,
    BTN_BACK,
    BTN_VOLUME_UP,
    BTN_VOLUME_DOWN,
    BTN_POWER
} button_t;

// 外部函数声明：从main.c中读取按钮状态
button_t get_pressed_button(void);

/**
 * @brief 初始化LVGL显示驱动
 * @return 显示设备指针
 */
lv_display_t* lvgl_display_init(void);

/**
 * @brief 刷新EPD显示（异步，非阻塞）
 */
void lvgl_display_refresh(void);

/**
 * @brief 强制完整刷新EPD（异步，非阻塞）
 */
void lvgl_display_refresh_full(void);

/**
 * @brief 初始化LVGL输入设备驱动
 * @return 输入设备指针
 */
lv_indev_t* lvgl_input_init(void);

/**
 * @brief LVGL tick任务
 * @param arg 任务参数
 */
void lvgl_tick_task(void *arg);

/**
 * @brief LVGL定时器任务
 * @param arg 任务参数
 */
void lvgl_timer_task(void *arg);

#endif // LVGL_DRIVER_H
