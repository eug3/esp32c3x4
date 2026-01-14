/**
 * @file file_browser_screen.h
 * @brief 文件浏览器屏幕 - 手绘 UI 版本
 */

#ifndef FILE_BROWSER_SCREEN_H
#define FILE_BROWSER_SCREEN_H

#include "screen_manager.h"

/**
 * @brief 初始化文件浏览器屏幕
 */
void file_browser_screen_init(void);

/**
 * @brief 获取文件浏览器屏幕实例
 * @return 屏幕指针
 */
screen_t* file_browser_screen_get_instance(void);

#endif // FILE_BROWSER_SCREEN_H
