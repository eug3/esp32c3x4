/**
 * @file epub_parser.h
 * @brief EPUB 电子书解析器模块
 */

#ifndef EPUB_PARSER_H
#define EPUB_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// EPUB 章节信息
typedef struct {
    char title[128];         // 章节标题
    char content_file[128];  // 内容文件路径（在 EPUB 内）
    long file_offset;        // 文件偏移（用于快速定位）
    int chapter_index;       // 章节索引
} epub_chapter_t;

// EPUB 元数据
typedef struct {
    char title[128];         // 书名
    char author[128];        // 作者
    char language[16];       // 语言
    char identifier[64];     // 唯一标识符
    int total_chapters;      // 总章节数
} epub_metadata_t;

// EPUB 阅读器位置
typedef struct {
    int current_chapter;     // 当前章节索引
    long chapter_position;   // 章节内位置
    int page_number;         // 当前页码
    int total_pages;         // 总页数
} epub_position_t;

// EPUB 阅读器状态
typedef struct {
    char epub_path[256];     // EPUB 文件路径
    FILE *current_file;      // 当前打开的内容文件
    epub_metadata_t metadata;// 元数据
    epub_chapter_t *chapters;// 章节列表
    epub_position_t position;// 当前位置
    bool is_open;            // 是否已打开
    bool is_unzipped;        // 是否已解压
    char extract_path[256];  // 解压路径
} epub_reader_t;

/**
 * @brief 初始化 EPUB 阅读器
 * @param reader 阅读器实例指针
 * @return true 成功，false 失败
 */
bool epub_parser_init(epub_reader_t *reader);

/**
 * @brief 打开 EPUB 文件
 * @param reader 阅读器实例指针
 * @param epub_path EPUB 文件路径
 * @return true 成功，false 失败
 */
bool epub_parser_open(epub_reader_t *reader, const char *epub_path);

/**
 * @brief 关闭 EPUB 文件
 * @param reader 阅读器实例指针
 */
void epub_parser_close(epub_reader_t *reader);

/**
 * @brief 获取 EPUB 元数据
 * @param reader 阅读器实例指针
 * @return 元数据指针
 */
const epub_metadata_t* epub_parser_get_metadata(const epub_reader_t *reader);

/**
 * @brief 获取章节数量
 * @param reader 阅读器实例指针
 * @return 章节数量
 */
int epub_parser_get_chapter_count(const epub_reader_t *reader);

/**
 * @brief 获取章节信息
 * @param reader 阅读器实例指针
 * @param chapter_index 章节索引
 * @return 章节信息指针，失败返回 NULL
 */
const epub_chapter_t* epub_parser_get_chapter(const epub_reader_t *reader, int chapter_index);

/**
 * @brief 读取章节文本内容
 * @param reader 阅读器实例指针
 * @param chapter_index 章节索引
 * @param text_buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 实际读取的字节数，失败返回 -1
 */
int epub_parser_read_chapter(const epub_reader_t *reader, int chapter_index,
                             char *text_buffer, size_t buffer_size);

/**
 * @brief 跳转到指定章节
 * @param reader 阅读器实例指针
 * @param chapter_index 章节索引
 * @return true 成功，false 失败
 */
bool epub_parser_goto_chapter(epub_reader_t *reader, int chapter_index);

/**
 * @brief 下一章
 * @param reader 阅读器实例指针
 * @return true 成功，false 已是最后一章
 */
bool epub_parser_next_chapter(epub_reader_t *reader);

/**
 * @brief 上一章
 * @param reader 阅读器实例指针
 * @return true 成功，false 已是第一章
 */
bool epub_parser_prev_chapter(epub_reader_t *reader);

/**
 * @brief 获取当前阅读位置
 * @param reader 阅读器实例指针
 * @return 当前位置信息
 */
epub_position_t epub_parser_get_position(const epub_reader_t *reader);

/**
 * @brief 保存阅读位置到 NVS
 * @param reader 阅读器实例指针
 * @return true 成功，false 失败
 */
bool epub_parser_save_position(const epub_reader_t *reader);

/**
 * @brief 从 NVS 加载阅读位置
 * @param reader 阅读器实例指针
 * @return true 成功且已跳转，false 无保存位置或失败
 */
bool epub_parser_load_position(epub_reader_t *reader);

/**
 * @brief 清理解析器资源
 * @param reader 阅读器实例指针
 */
void epub_parser_cleanup(epub_reader_t *reader);

/**
 * @brief 验证文件是否为有效的 EPUB
 * @param file_path 文件路径
 * @return true 是 EPUB，false 不是
 */
bool epub_parser_is_valid_epub(const char *file_path);

#ifdef __cplusplus
}
#endif

#endif // EPUB_PARSER_H
