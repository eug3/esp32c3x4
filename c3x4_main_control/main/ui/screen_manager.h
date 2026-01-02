/**
 * @file screen_manager.h
 * @brief 屏幕导航管理器 - 处理不同屏幕之间的切换
 */

#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include "lvgl.h"

// 前置声明
struct screen_context;

// 屏幕上下文 - 包含系统状态信息
typedef struct screen_context {
    uint32_t battery_mv;
    uint8_t battery_pct;
    bool charging;
    const char *version_str;
    lv_indev_t *indev;

    // 函数指针 - 用于获取系统状态
    uint32_t (*read_battery_voltage_mv)(void);
    uint8_t (*read_battery_percentage)(void);
    bool (*is_charging)(void);
} screen_context_t;

/**
 * @brief 初始化屏幕管理器
 * @param ctx 屏幕上下文指针
 */
void screen_manager_init(screen_context_t *ctx);

/**
 * @brief 显示首页
 */
void screen_manager_show_index(void);

/**
 * @brief 显示文件浏览器
 */
void screen_manager_show_file_browser(void);

/**
 * @brief 获取屏幕管理器上下文
 * @return 屏幕上下文指针
 */
screen_context_t* screen_manager_get_context(void);

#endif // SCREEN_MANAGER_H
