/**
 * @file chinese_font_impl.h
 * @brief 简单中文字体实现 - 基于 DIY LVGL 字体格式
 *
 * 字体格式说明：
 * - LVGL binfont 格式
 * - 支持 UTF-8 输入
 * - 1bpp 位图
 */

#ifndef CHINESE_FONT_IMPL_H
#define CHINESE_FONT_IMPL_H

#include <stdint.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/

#define CHINESE_FONT_SIZE 16

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 中文字符位图信息
 */
typedef struct {
    uint32_t glyph_index;   // 字形索引
    uint8_t *bitmap;        // 位图数据指针
    uint8_t width;          // 宽度
    uint8_t height;         // 高度
    uint8_t bpp;            // 位深度（通常为 1）
} chinese_glyph_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化中文字体系统
 * @return true 成功，false 失败
 */
bool chinese_font_init(void);

/**
 * @brief 检查字符是否在中文字体中
 * @param ch UTF-32 字符
 * @return true 存在，false 不存在
 */
bool chinese_font_has_char(uint32_t ch);

/**
 * @brief 获取字符位图
 * @param ch UTF-32 字符
 * @param glyph 输出字形信息
 * @return true 成功，false 失败
 */
bool chinese_font_get_glyph(uint32_t ch, chinese_glyph_t *glyph);

/**
 * @brief 渲染单个中文字符到帧缓冲
 * @param x X 坐标
 * @param y Y 坐标
 * @param ch UTF-32 字符
 * @param color 前景色（0=黑，1=白）
 * @param framebuffer 帧缓冲指针
 * @param fb_width 帧缓冲宽度
 * @param fb_height 帧缓冲高度
 * @return 渲染的字符宽度
 */
int chinese_font_render_char(int x, int y, uint32_t ch, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height);

/**
 * @brief 渲染 UTF-8 文本
 * @param x X 坐标
 * @param y Y 坐标
 * @param text UTF-8 文本
 * @param color 前景色（0=黑，1=白）
 * @param framebuffer 帧缓冲指针
 * @param fb_width 帧缓冲宽度
 * @param fb_height 帧缓冲高度
 * @return 渲染的文本宽度
 */
int chinese_font_render_text(int x, int y, const char *text, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height);

/**
 * @brief 获取文本宽度
 * @param text UTF-8 文本
 * @return 文本宽度（像素）
 */
int chinese_font_get_text_width(const char *text);

/**
 * @brief 获取字体高度
 * @return 字体高度（像素）
 */
int chinese_font_get_height(void);

/**
 * @brief UTF-8 转换为 UTF-32
 * @param utf8 UTF-8 字符串指针
 * @param out_utf32 输出 UTF-32 字符
 * @return 下一个字符的偏移量（字节数）
 */
int chinese_font_utf8_to_utf32(const char *utf8, uint32_t *out_utf32);

#endif // CHINESE_FONT_IMPL_H
