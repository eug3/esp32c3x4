/**
 * @file font_renderer.h
 * @brief 字体渲染系统 - 支持 ASCII 和中文字符
 *
 * 参考项目：diy-esp32-epub-reader
 * 字体来源：
 * - ASCII: GUI_Paint 内置字体 (Font8/12/16/20/24)
 * - 中文: 自定义位图字体（.bin 文件或编译到固件）
 */

#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include <stdint.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/

// 字体大小
#define FONT_SIZE_8    8
#define FONT_SIZE_12   12
#define FONT_SIZE_14   14
#define FONT_SIZE_16   16
#define FONT_SIZE_18   18
#define FONT_SIZE_20   20
#define FONT_SIZE_24   24
#define FONT_SIZE_28   28

// 最大字体路径长度
#define MAX_FONT_PATH 256

// 默认字体大小
#define DEFAULT_FONT_SIZE FONT_SIZE_16

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 字体信息结构
 */
typedef struct {
    int size;                   // 字体大小
    int width;                  // 字符宽度（等宽）
    int height;                 // 字符高度
    bool is_loaded;             // 是否已加载
    char font_path[MAX_FONT_PATH];  // 字体文件路径（如果从文件加载）
} font_info_t;

/**
 * @brief 字体度量信息
 */
typedef struct {
    int width;      // 文本宽度（像素）
    int height;     // 文本高度（像素）
    int baseline;   // 基线位置
} font_metrics_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化字体渲染器
 * @return true 成功，false 失败
 */
bool font_renderer_init(void);

/**
 * @brief 反初始化字体渲染器
 */
void font_renderer_deinit(void);

/**
 * @brief 设置当前字体大小
 * @param font_size 字体大小
 * @return true 成功，false 失败
 */
bool font_renderer_set_size(int font_size);

/**
 * @brief 获取当前字体大小
 * @return 字体大小
 */
int font_renderer_get_size(void);

/**
 * @brief 加载中文字体文件
 * @param font_path 字体文件路径（.bin 格式）
 * @param font_size 字体大小
 * @return true 成功，false 失败
 */
bool font_renderer_load_chinese_font(const char *font_path, int font_size);

/**
 * @brief 检查字体是否已加载
 * @param font_size 字体大小
 * @return true 已加载，false 未加载
 */
bool font_renderer_is_loaded(int font_size);

/**
 * @brief 获取字体信息
 * @param font_size 字体大小
 * @return 字体信息指针，未找到返回 NULL
 */
const font_info_t* font_renderer_get_info(int font_size);

/**
 * @brief 获取文本度量
 * @param text 文本（UTF-8 编码）
 * @param font_size 字体大小
 * @param metrics 输出度量信息
 * @return true 成功，false 失败
 */
bool font_renderer_get_metrics(const char *text, int font_size, font_metrics_t *metrics);

/**
 * @brief 渲染单个字符到缓冲区
 * @param ch 字符（UTF-32）
 * @param font_size 字体大小
 * @param output 输出缓冲区（位图数据，1bpp）
 * @param output_size 输出缓冲区大小
 * @param width 输出字符宽度
 * @param height 输出字符高度
 * @return true 成功，false 失败
 */
bool font_renderer_render_char(uint32_t ch, int font_size,
                                uint8_t *output, size_t output_size,
                                int *width, int *height);

/**
 * @brief 渲染文本到缓冲区
 * @param text 文本（UTF-8 编码）
 * @param font_size 字体大小
 * @param output 输出缓冲区（位图数据，1bpp）
 * @param output_size 输出缓冲区大小
 * @param max_width 最大宽度（0 表示不限制）
 * @param metrics 输出度量信息
 * @return true 成功，false 失败
 */
bool font_renderer_render_text(const char *text, int font_size,
                                uint8_t *output, size_t output_size,
                                int max_width, font_metrics_t *metrics);

/**
 * @brief 从文件加载中文字体并渲染字符
 * @param ch 字符（UTF-32）
 * @param font_path 字体文件路径
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区大小
 * @param width 输出字符宽度
 * @param height 输出字符高度
 * @return true 成功，false 失败
 */
bool font_renderer_render_char_from_file(uint32_t ch, const char *font_path,
                                          uint8_t *output, size_t output_size,
                                          int *width, int *height);

/**
 * @brief 检查是否为中文字符
 * @param ch UTF-32 字符
 * @return true 是中文，false 不是
 */
static inline bool font_renderer_is_chinese(uint32_t ch)
{
    // CJK 统一汉字范围：U+4E00 - U+9FFF
    return (ch >= 0x4E00 && ch <= 0x9FFF);
}

/**
 * @brief UTF-8 转换为 UTF-32
 * @param utf8 UTF-8 字符串指针
 * @param out_utf32 输出 UTF-32 字符
 * @return 下一个字符的偏移量（字节数）
 */
int font_renderer_utf8_to_utf32(const char *utf8, uint32_t *out_utf32);

/**
 * @brief 获取 UTF-8 字符的字节长度
 * @param ch UTF-8 首字节
 * @return 字符长度（1-4）
 */
static inline int font_renderer_utf8_char_len(char ch)
{
    if ((ch & 0x80) == 0) return 1;       // 0xxxxxxx
    if ((ch & 0xE0) == 0xC0) return 2;     // 110xxxxx
    if ((ch & 0xF0) == 0xE0) return 3;     // 1110xxxx
    if ((ch & 0xF8) == 0xF0) return 4;     // 11110xxx
    return 1;  // 无效 UTF-8
}

/**
 * @brief 扫描 SD 卡字体目录
 * @param font_dir 字体目录路径
 * @return 找到的字体数量
 */
int font_renderer_scan_directory(const char *font_dir);

#endif // FONT_RENDERER_H
