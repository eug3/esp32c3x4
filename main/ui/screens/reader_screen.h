/**
 * @file reader_screen.h
 * @brief 阅读器屏幕 - 手绘 UI 版本
 */

#ifndef READER_SCREEN_H
#define READER_SCREEN_H

#include "screen_manager.h"

/**
 * @brief 初始化阅读器屏幕
 */
void reader_screen_init(void);

/**
 * @brief 清理阅读器屏幕并释放内存
 */
void reader_screen_deinit(void);

/**
 * @brief 获取阅读器屏幕实例
 * @return 屏幕指针
 */
screen_t* reader_screen_get_instance(void);

#endif // READER_SCREEN_H
