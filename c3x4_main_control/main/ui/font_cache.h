/**
 * @file font_cache.h
 * @brief 字体智能缓存系统（LittleFS + SD 卡分级读取）
 */

#ifndef FONT_CACHE_H
#define FONT_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化字体缓存系统
 * 
 * 功能：
 * - 检查 /littlefs/fonts/ 目录
 * - 如果缓存不存在或参数不匹配，从 SD 卡字体文件生成缓存
 * - 缓存优先从 LittleFS 命中，未命中从 SD 字体读取
 *
 * 缓存模式：
 * - 默认：连续区间缓存（U+0000..U+0BB7，共 3000）
 * - 可选：固件内置字表缓存（例如《通用规范汉字表》一级 3500 + 标点）
 *   通过生成的 font_cache_level1_table.h 提供 codepoint 数组。
 * 
 * @param sd_font_path SD 卡完整字体路径（如 /sdcard/fonts/msyh_19x25.bin）
 * @return true 成功, false 失败
 */
bool font_cache_init(const char *sd_font_path);

/**
 * @brief 获取字形数据（分级缓存读取）
 * 
 * 读取顺序：
 * 1. 尝试从 LittleFS 常用字缓存读取
 * 2. 未命中则从 SD 卡完整字体读取
 * 
 * @param unicode UTF-32 字符编码
 * @param buffer 输出缓冲区（至少 75 字节）
 * @param buf_size 缓冲区大小
 * @return 实际读取字节数，0 表示失败
 */
int font_cache_get_glyph(uint32_t unicode, uint8_t *buffer, size_t buf_size);

/**
 * @brief 获取缓存统计信息
 * 
 * @param hits 缓存命中次数（可选，传 NULL 忽略）
 * @param misses 缓存未命中次数（可选，传 NULL 忽略）
 * @param cached_chars 已缓存字符数（可选，传 NULL 忽略）
 */
void font_cache_get_stats(uint32_t *hits, uint32_t *misses, uint32_t *cached_chars);

/**
 * @brief 清理字体缓存（释放资源）
 */
void font_cache_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // FONT_CACHE_H
