/**
 * @file chapter_buffer.h
 * @brief 章节 Flash 缓冲区 - 使用 mmap 实现零拷贝访问
 * 
 * 架构设计：
 * 1. 2MB Flash 分区作为章节缓冲区
 * 2. 从 SD 卡拷贝当前章节到 Flash
 * 3. mmap 挂载后直接通过指针访问
 * 4. 实现极速翻页体验
 * 
 * 分区布局 (2MB = 0x200000):
 * +------------------+
 * | 元数据区 4KB     |  <- 记录当前缓存的章节信息
 * +------------------+
 * | 章节内容区       |  <- 最大约 2MB-4KB
 * +------------------+
 */

#ifndef CHAPTER_BUFFER_H
#define CHAPTER_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 章节缓冲区大小 (2MB)
#define CHAPTER_BUFFER_SIZE         (2 * 1024 * 1024)
// 元数据区大小 (4KB，一个 Flash 扇区)
#define CHAPTER_BUFFER_META_SIZE    (4096)
// 最大章节内容大小
#define CHAPTER_BUFFER_MAX_CONTENT  (CHAPTER_BUFFER_SIZE - CHAPTER_BUFFER_META_SIZE)

// 缓冲区状态
typedef enum {
    CHAPTER_BUF_STATE_EMPTY = 0,        // 空闲
    CHAPTER_BUF_STATE_LOADING,          // 正在加载
    CHAPTER_BUF_STATE_READY,            // 就绪可读
    CHAPTER_BUF_STATE_ERROR             // 错误状态
} chapter_buffer_state_t;

// 章节缓冲区元数据 (存储在 Flash 分区开头)
typedef struct __attribute__((packed)) {
    uint32_t magic;                     // 魔数 0x43485054 ("CHPT")
    uint32_t version;                   // 版本号
    uint32_t state;                     // 缓冲区状态
    uint32_t content_size;              // 实际内容大小
    uint32_t content_crc32;             // 内容 CRC32 校验
    char epub_path[128];                // EPUB 文件路径
    uint32_t chapter_index;             // 章节索引
    char content_file[128];             // 章节内容文件名
    uint32_t reserved[16];              // 保留字段
} chapter_buffer_meta_t;

/**
 * @brief 初始化章节缓冲区
 * @return true 成功, false 失败
 */
bool chapter_buffer_init(void);

/**
 * @brief 反初始化章节缓冲区
 */
void chapter_buffer_deinit(void);

/**
 * @brief 获取当前缓冲区状态
 * @return 缓冲区状态
 */
chapter_buffer_state_t chapter_buffer_get_state(void);

/**
 * @brief 检查指定章节是否已缓存
 * @param epub_path EPUB 文件路径
 * @param chapter_index 章节索引
 * @return true 已缓存, false 未缓存
 */
bool chapter_buffer_is_cached(const char *epub_path, int chapter_index);

/**
 * @brief 从 SD 卡加载章节到 Flash 缓冲区
 * @param epub_path EPUB 文件路径
 * @param chapter_index 章节索引
 * @param content_file 章节内容文件名 (ZIP 内路径)
 * @param data 章节数据
 * @param size 数据大小
 * @return true 成功, false 失败
 */
bool chapter_buffer_load(const char *epub_path, int chapter_index, 
                         const char *content_file, const uint8_t *data, size_t size);

/**
 * @brief 直接从 ZIP 解压加载章节到 Flash 缓冲区
 * @param epub_path EPUB 文件路径
 * @param chapter_index 章节索引
 * @return true 成功, false 失败
 */
bool chapter_buffer_load_from_epub(const char *epub_path, int chapter_index);

/**
 * @brief 获取章节内容的只读指针 (mmap 零拷贝)
 * @param out_size 输出: 内容大小
 * @return 章节内容指针, 失败返回 NULL
 * 
 * @note 返回的指针直接指向 Flash，只读且无需释放
 */
const char *chapter_buffer_get_content(size_t *out_size);

/**
 * @brief 获取当前缓存的章节信息
 * @param out_epub_path 输出: EPUB 路径 (可为 NULL)
 * @param out_chapter_index 输出: 章节索引 (可为 NULL)
 * @return true 有缓存, false 无缓存
 */
bool chapter_buffer_get_info(char *out_epub_path, int *out_chapter_index);

/**
 * @brief 清空章节缓冲区
 */
void chapter_buffer_clear(void);

/**
 * @brief 获取缓冲区统计信息
 * @param out_total_size 输出: 总大小
 * @param out_used_size 输出: 已使用大小
 * @param out_load_count 输出: 加载次数
 */
void chapter_buffer_get_stats(size_t *out_total_size, size_t *out_used_size, 
                              uint32_t *out_load_count);

#ifdef __cplusplus
}
#endif

#endif // CHAPTER_BUFFER_H
