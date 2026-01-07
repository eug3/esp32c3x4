/**
 * @file font_select_screen.h
 * @brief 字体选择屏幕
 */

#ifndef FONT_SELECT_SCREEN_H
#define FONT_SELECT_SCREEN_H

#include "screen_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化字体选择屏幕
 */
void font_select_screen_init(void);

/**
 * @brief 获取字体选择屏幕实例
 * @return 屏幕指针
 */
screen_t* font_select_screen_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif // FONT_SELECT_SCREEN_H
