/**
 * @file lvgl_demo.h
 * @brief LVGL GUI - 主菜单屏幕
 */

#ifndef LVGL_DEMO_H
#define LVGL_DEMO_H

#include "lvgl.h"

/**
 * @brief 创建 Monster For Pan 菜单屏幕（包含系统信息、电池和菜单）
 * @param battery_mv 电池电压 (mV)
 * @param battery_pct 电池百分比
 * @param charging 是否正在充电
 * @param version_str 版本字符串
 */
void lvgl_demo_create_welcome_screen(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str);

#endif // LVGL_DEMO_H
