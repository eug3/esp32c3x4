/**
 * @file wallpaper_screen.h
 * @brief 壁纸管理屏幕（列表 + 预览 + 选择）
 */

#ifndef WALLPAPER_SCREEN_H
#define WALLPAPER_SCREEN_H

#include "screen_manager.h"

void wallpaper_screen_init(void);
screen_t* wallpaper_screen_get_instance(void);

#endif // WALLPAPER_SCREEN_H
