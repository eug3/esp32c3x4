/**
 * @file index_screen.h
 * @brief 首页 - Monster For Pan 主菜单
 */

#ifndef INDEX_SCREEN_H
#define INDEX_SCREEN_H

#include "lvgl.h"

/**
 * @brief 创建首页屏幕（包含系统信息、电池和菜单）
 * @param battery_mv 电池电压 (mV)
 * @param battery_pct 电池百分比
 * @param charging 是否正在充电
 * @param version_str 版本字符串
 * @param indev 输入设备指针（用于设置焦点）
 */
void index_screen_create(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str, lv_indev_t *indev);

#endif // INDEX_SCREEN_H
