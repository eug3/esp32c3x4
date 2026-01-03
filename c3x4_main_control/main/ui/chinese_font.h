/**
 * @file chinese_font.h
 * @brief 内置中文字体库
 *
 * 使用 tools/gen_chinese_font.py 生成
 * 包含约 500 个常用字符，适合基础 UI 使用
 */
#ifndef CHINESE_FONT_H
#define CHINESE_FONT_H

#include <stdint.h>
#include <lvgl.h>
#include <stdbool.h>

/* 字体信息 */
#define CHINESE_FONT_SIZE     16
#define CHINESE_FONT_BPP      1

/**
 * @brief 获取内置中文字体
 * @return lv_font_t* 字体指针，失败返回 NULL
 */
lv_font_t *chinese_font_get(void);

/**
 * @brief 检查内置字体是否可用
 * @return true 可用, false 不可用
 */
bool chinese_font_is_available(void);

/**
 * @brief 释放字体资源
 */
void chinese_font_release(void);

#endif // CHINESE_FONT_H
