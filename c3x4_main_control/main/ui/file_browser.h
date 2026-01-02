/**
 * @file file_browser.h
 * @brief SD 卡文件浏览器屏幕
 */

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include "lvgl.h"

/**
 * @brief 创建 SD 卡文件浏览器页面
 * @param indev 输入设备指针（用于设置焦点）
 */
void file_browser_screen_create(lv_indev_t *indev);

#endif // FILE_BROWSER_H
