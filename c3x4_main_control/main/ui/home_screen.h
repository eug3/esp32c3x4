/**
 * @file home_screen.h
 * @brief 首页屏幕 - 手绘 UI 版本
 */

#ifndef HOME_SCREEN_H
#define HOME_SCREEN_H

#include "screen_manager.h"

// 菜单项
typedef enum {
    MENU_ITEM_FILE_BROWSER = 0,  // 文件浏览器
    MENU_ITEM_SETTINGS,           // 设置
    MENU_ITEM_COUNT
} menu_item_t;

/**
 * @brief 初始化首页屏幕
 */
void home_screen_init(void);

/**
 * @brief 获取首页屏幕实例
 * @return 屏幕指针
 */
screen_t* home_screen_get_instance(void);

/**
 * @brief 测试局刷功能 - 绘制一个成比例的矩形框
 */
void test_partial_refresh_rect(void);

#endif // HOME_SCREEN_H
