/**
 * @file xt_eink_font_impl.h
 * @brief XTEink 字体渲染实现 - 用于手绘 UI 系统
 *
 * 提供与 chinese_font_impl.h 相同的接口，但使用 XTEink 字体格式
 */

#ifndef XT_EINK_FONT_IMPL_H
#define XT_EINK_FONT_IMPL_H

#include <stdint.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 中文字符位图信息
 */
typedef struct {
    uint8_t *bitmap;        // 位图数据指针
    uint8_t width;          // 宽度
    uint8_t height;         // 高度
} xt_eink_glyph_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化字体系统
 * @return true 成功，false 失败
 */
bool xt_eink_font_init(void);

/**
 * @brief 释放字体系统资源
 */
void xt_eink_font_deinit(void);

/**
 * @brief 检查字符是否在字体中
 * @param ch UTF-32 字符
 * @return true 存在，false 不存在
 */
bool xt_eink_font_has_char(uint32_t ch);

/**
 * @brief 获取字符位图
 * @param ch UTF-32 字符
 * @param glyph 输出字形信息
 * @return true 成功，false 失败
 */
bool xt_eink_font_get_glyph(uint32_t ch, xt_eink_glyph_t *glyph);

/**
 * @brief 渲染单个字符到帧缓冲
 * @param x X 坐标
 * @param y Y 坐标
 * @param ch UTF-32 字符
 * @param color 前景色
 * @param framebuffer 帧缓冲指针
 * @param fb_width 帧缓冲宽度
 * @param fb_height 帧缓冲高度
 * @return 渲染的字符宽度
 */
int xt_eink_font_render_char(int x, int y, uint32_t ch, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height);

/**
 * @brief 渲染 UTF-8 文本
 * @param x X 坐标
 * @param y Y 坐标
 * @param text UTF-8 文本
 * @param color 前景色
 * @param framebuffer 帧缓冲指针
 * @param fb_width 帧缓冲宽度
 * @param fb_height 帧缓冲高度
 * @return 渲染的文本宽度
 */
int xt_eink_font_render_text(int x, int y, const char *text, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height);

/**
 * @brief 获取文本宽度
 * @param text UTF-8 文本
 * @return 文本宽度（像素）
 */
int xt_eink_font_get_text_width(const char *text);

/**
 * @brief 获取字体高度
 * @return 字体高度（像素）
 */
int xt_eink_font_get_height(void);

/**
 * @brief 获取当前字体路径
 * @return 当前字体路径字符串
 */
const char *xt_eink_font_get_current_path(void);

/**
 * @brief 获取菜单默认字体（不受用户字体切换影响）
 *
 * 菜单界面应使用此函数获取字体对象，以确保菜单显示始终使用
 * 启动时初始化的默认字体，不会因用户切换字体而改变。
 *
 * @return 菜单默认字体对象指针，若未初始化则返回 NULL
 */
void *xt_eink_font_get_menu_default_font(void);

/**
 * @brief 检查菜单字体中是否包含指定字符
 *
 * 使用菜单默认字体而不是当前用户字体。
 *
 * @param ch UTF-32 字符
 * @return true 存在，false 不存在
 */
bool xt_eink_font_menu_has_char(uint32_t ch);

/**
 * @brief 获取菜单字体中字符的位图
 *
 * 使用菜单默认字体而不是当前用户字体。
 *
 * @param ch UTF-32 字符
 * @param glyph 输出字形信息
 * @return true 成功，false 失败
 */
bool xt_eink_font_menu_get_glyph(uint32_t ch, xt_eink_glyph_t *glyph);

/**
 * @brief 获取菜单字体高度
 *
 * 使用菜单默认字体而不是当前用户字体。
 *
 * @return 字体高度（像素）
 */
int xt_eink_font_menu_get_height(void);

/**
 * @brief 设置当前字体路径（不重新加载）
 * @param path 字体文件路径
 */
void xt_eink_font_set_current_path(const char *path);

/**
 * @brief 重新加载字体（用于切换字体后）
 * @param path 字体文件路径
 * @return true 成功，false 失败
 */
bool xt_eink_font_reload(const char *path);

/**
 * @brief UTF-8 转换为 UTF-32
 * @param utf8 UTF-8 字符串指针
 * @param out_utf32 输出 UTF-32 字符
 * @return 下一个字符的偏移量（字节数）
 */
int xt_eink_font_utf8_to_utf32(const char *utf8, uint32_t *out_utf32);

#endif // XT_EINK_FONT_IMPL_H
