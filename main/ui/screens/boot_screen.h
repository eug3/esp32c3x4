/**
 * @file boot_screen.h
 * @brief 启动屏幕 - 带动画的启动画面，在后台初始化时持续显示
 */

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include "screen_manager.h"
#include <stdbool.h>

/**
 * @brief 初始化启动屏幕
 */
void boot_screen_init(void);

/**
 * @brief 获取启动屏幕实例
 * @return 屏幕指针
 */
screen_t* boot_screen_get_instance(void);

/**
 * @brief 设置启动状态文本
 * @param status 状态文本（ASCII）
 */
void boot_screen_set_status(const char *status);

/**
 * @brief 标记初始化完成，准备切换到主屏幕
 */
void boot_screen_complete(void);

/**
 * @brief 检查启动屏幕是否已完成初始化
 * @return true 已完成, false 未完成
 */
bool boot_screen_is_completed(void);

#endif // BOOT_SCREEN_H
