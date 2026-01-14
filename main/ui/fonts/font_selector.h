/**
 * @file font_selector.h
 * @brief 字体选择器 - 扫描并管理可用字体
 */

#ifndef FONT_SELECTOR_H
#define FONT_SELECTOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_SELECTOR_MAX_FONTS 16
#define FONT_NAME_MAX_LEN 64

/**
 * @brief 字体信息
 */
typedef struct {
    char path[128];           // 完整路径
    char name[FONT_NAME_MAX_LEN];  // 显示名称
    uint16_t width;           // 字体宽度
    uint16_t height;          // 字体高度
    uint32_t file_size;       // 文件大小
} font_info_t;

/**
 * @brief 扫描可用字体
 * @param fonts 输出字体数组
 * @param max_fonts 最大字体数量
 * @return 扫描到的字体数量
 */
int font_selector_scan_fonts(font_info_t *fonts, int max_fonts);

/**
 * @brief 获取字体数量
 * @return 扫描到的字体数量
 */
int font_selector_get_count(void);

/**
 * @brief 获取指定索引的字体信息
 * @param index 字体索引
 * @return 字体信息指针，失败返回NULL
 */
const font_info_t *font_selector_get_font(int index);

/**
 * @brief 根据路径查找字体索引
 * @param path 字体路径
 * @return 字体索引，未找到返回-1
 */
int font_selector_find_by_path(const char *path);

/**
 * @brief 检查路径是否为有效字体文件
 * @param path 文件路径
 * @return true 是有效字体，false 不是
 */
bool font_selector_is_valid_font(const char *path);

#ifdef __cplusplus
}
#endif

#endif // FONT_SELECTOR_H
