/**
 * @file lvgl_demo.h
 * @brief LVGL示例GUI应用头文件
 */

#ifndef LVGL_DEMO_H
#define LVGL_DEMO_H

#include "lvgl.h"

/**
 * @brief 创建主屏幕（带按钮）
 */
void lvgl_demo_create_main_screen(void);

/**
 * @brief 创建菜单屏幕（列表）
 */
void lvgl_demo_create_menu_screen(void);

/**
 * @brief 创建信息显示屏幕
 * @param title 标题文本
 * @param info_text 信息文本
 */
void lvgl_demo_create_info_screen(const char *title, const char *info_text);

/**
 * @brief 创建进度条示例屏幕
 */
void lvgl_demo_create_progress_screen(void);

/**
 * @brief 创建启动画面
 */
void lvgl_demo_create_splash_screen(void);

#endif // LVGL_DEMO_H
