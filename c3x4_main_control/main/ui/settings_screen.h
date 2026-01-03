/**
 * @file settings_screen.h
 * @brief 设置页面 - 字体选择和其他设置
 */

#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include "lvgl.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建设置屏幕
 * @param indev 输入设备指针
 */
void settings_screen_create(lv_indev_t *indev);

#endif // SETTINGS_SCREEN_H
