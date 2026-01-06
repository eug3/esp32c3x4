/**
 * @file epub_cache.h
 * @brief EPUB Flash 缓存管理器
 *
 * 使用 Flash 作为二级缓存，减少 SD 卡访问
 * 架构: RAM → Flash → SD 卡
 */

#ifndef EPUB_CACHE_H
#define EPUB_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 缓存配置
#define EPUB_CACHE_MAX_SIZE   (2 * 1024 * 1024)  // 2MB Flash 缓存
#define EPUB_CACHE_CHUNK_SIZE 4096                 // 每块 4KB

// 缓存项类型
typedef enum {
    EPUB_CACHE_CHAPTER,      // 章节文本
    EPUB_CACHE_METADATA,     // 元数据
    EPUB_CACHE_IMAGE,        // 图片
    EPUB_CACHE_INDEX,        // 章节索引
} epub_cache_type_t;

// 缓存键
typedef struct {
    char epub_path[256];     // EPUB 文件路径
    char content_path[256];  // 内容路径（如 OEBPS/chapter1.xhtml）
    epub_cache_type_t type;  // 缓存类型
} epub_cache_key_t;

/**
 * @brief 初始化 Flash 缓存
 * @return true 成功，false 失败
 */
bool epub_cache_init(void);

/**
 * @brief 检查缓存中是否存在数据
 * @param key 缓存键
 * @return true 存在，false 不存在
 */
bool epub_cache_exists(const epub_cache_key_t *key);

/**
 * @brief 从缓存读取数据
 * @param key 缓存键
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 实际读取的字节数，失败返回 -1
 */
int epub_cache_read(const epub_cache_key_t *key, void *buffer, size_t buffer_size);

/**
 * @brief 写入数据到缓存
 * @param key 缓存键
 * @param data 数据
 * @param data_size 数据大小
 * @return true 成功，false 失败
 */
bool epub_cache_write(const epub_cache_key_t *key, const void *data, size_t data_size);

/**
 * @brief 删除缓存项
 * @param key 缓存键
 * @return true 成功，false 失败
 */
bool epub_cache_delete(const epub_cache_key_t *key);

/**
 * @brief 清空所有缓存
 * @return true 成功，false 失败
 */
bool epub_cache_clear(void);

/**
 * @brief 获取缓存使用情况
 * @param used 输出已使用字节数
 * @param total 输出总字节数
 * @return true 成功，false 失败
 */
bool epub_cache_get_usage(size_t *used, size_t *total);

/**
 * @brief 预缓存整个章节到 Flash
 * @param epub_path EPUB 文件路径
 * @param chapter_path 章节在 EPUB 中的路径
 * @param data 章节数据
 * @param data_size 数据大小
 * @return true 成功，false 失败
 */
bool epub_cache_precache_chapter(const char *epub_path,
                                  const char *chapter_path,
                                  const void *data,
                                  size_t data_size);

/**
 * @brief 获取缓存项对应的实际文件路径（LittleFS 上）
 * @param key 缓存键
 * @param out_path 输出路径缓冲区
 * @param out_size 缓冲区大小
 * @return true 成功，false 失败
 */
bool epub_cache_get_file_path(const epub_cache_key_t *key, char *out_path, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // EPUB_CACHE_H
