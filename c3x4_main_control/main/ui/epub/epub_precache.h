/**
 * @file epub_precache.h
 * @brief EPUB 章节预缓存管理器 - 滑动窗口机制
 *
 * 在阅读过程中自动预解压几章到 littlefs，保持一个滑动窗口，
 * 让翻页响应更快，无需每次都从 SD 卡解压
 */

#ifndef EPUB_PRECACHE_H
#define EPUB_PRECACHE_H

#include <stdbool.h>
#include <stdint.h>
#include "epub_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// 预缓存窗口配置
#define PRECACHE_WINDOW_BEFORE 2   // 当前章节之前缓存章节数
#define PRECACHE_WINDOW_AFTER  5   // 当前章节之后缓存章节数
#define PRECACHE_MAX_CHAPTERS  10  // 最大同时缓存章节数（避免占满 Flash）

/**
 * @brief 初始化预缓存管理器
 * @return true 成功，false 失败
 */
bool epub_precache_init(void);

/**
 * @brief 更新预缓存窗口
 * 
 * 根据当前章节索引，自动缓存周围的章节（前PRECACHE_WINDOW_BEFORE章，
 * 后PRECACHE_WINDOW_AFTER章），并清理窗口外的缓存。
 * 
 * @param reader EPUB 阅读器实例
 * @param current_chapter 当前章节索引
 * @return true 成功，false 失败
 */
bool epub_precache_update_window(const epub_reader_t *reader, int current_chapter);

/**
 * @brief 预缓存指定章节
 * 
 * 将指定章节从 EPUB 中解压并缓存到 littlefs。
 * 如果已缓存，则跳过。
 * 
 * @param reader EPUB 阅读器实例
 * @param chapter_index 章节索引
 * @return true 成功或已缓存，false 失败
 */
bool epub_precache_chapter(const epub_reader_t *reader, int chapter_index);

/**
 * @brief 清理窗口外的缓存章节
 * 
 * 删除不在 [current - BEFORE, current + AFTER] 窗口内的缓存章节。
 * 
 * @param reader EPUB 阅读器实例
 * @param current_chapter 当前章节索引
 * @return true 成功，false 失败
 */
bool epub_precache_cleanup_outside_window(const epub_reader_t *reader, int current_chapter);

/**
 * @brief 清空所有预缓存
 * 
 * 删除当前 EPUB 的所有缓存章节。
 * 
 * @param reader EPUB 阅读器实例
 * @return true 成功，false 失败
 */
bool epub_precache_clear_all(const epub_reader_t *reader);

/**
 * @brief 获取预缓存统计信息
 * 
 * @param total_cached 输出当前缓存的章节数
 * @param total_size 输出缓存总字节数
 * @return true 成功，false 失败
 */
bool epub_precache_get_stats(int *total_cached, size_t *total_size);

#ifdef __cplusplus
}
#endif

#endif // EPUB_PRECACHE_H
