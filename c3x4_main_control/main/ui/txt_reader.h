/**
 * @file txt_reader.h
 * @brief TXT 文本文件阅读器模块
 */

#ifndef TXT_READER_H
#define TXT_READER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// TXT 文件编码类型
typedef enum {
    TXT_ENCODING_UTF8,      // UTF-8 编码
    TXT_ENCODING_GB18030,   // GB18030/GBK 编码
    TXT_ENCODING_ASCII,     // ASCII 编码
    TXT_ENCODING_AUTO       // 自动检测
} txt_encoding_t;

// 阅读器位置信息
typedef struct {
    long file_position;      // 当前文件位置
    int page_number;         // 当前页码
    int total_pages;         // 总页数（估算）
    long file_size;          // 文件大小
} txt_position_t;

// TXT 阅读器状态
typedef struct {
    FILE *file;              // 文件句柄
    char file_path[256];     // 文件路径
    txt_encoding_t encoding; // 文件编码
    txt_position_t position; // 当前位置
    bool is_open;            // 是否已打开
    uint8_t *buffer;         // 读取缓冲区
    size_t buffer_size;      // 缓冲区大小
} txt_reader_t;

/**
 * @brief 初始化 TXT 阅读器
 * @param reader 阅读器实例指针
 * @return true 成功，false 失败
 */
bool txt_reader_init(txt_reader_t *reader);

/**
 * @brief 打开 TXT 文件
 * @param reader 阅读器实例指针
 * @param file_path 文件路径
 * @param encoding 文件编码（TXT_ENCODING_AUTO 自动检测）
 * @return true 成功，false 失败
 */
bool txt_reader_open(txt_reader_t *reader, const char *file_path, txt_encoding_t encoding);

/**
 * @brief 关闭 TXT 文件
 * @param reader 阅读器实例指针
 */
void txt_reader_close(txt_reader_t *reader);

/**
 * @brief 读取下一页文本
 * @param reader 阅读器实例指针
 * @param text_buffer 输出文本缓冲区
 * @param buffer_size 缓冲区大小（字节）
 * @param chars_per_page 每页字符数（估算）
 * @return 实际读取的字符数
 */
int txt_reader_read_page(txt_reader_t *reader, char *text_buffer, size_t buffer_size, int chars_per_page);

/**
 * @brief 跳转到指定页码
 * @param reader 阅读器实例指针
 * @param page_number 目标页码
 * @return true 成功，false 失败
 */
bool txt_reader_goto_page(txt_reader_t *reader, int page_number);

/**
 * @brief 跳转到文件位置
 * @param reader 阅读器实例指针
 * @param position 文件位置（字节偏移）
 * @return true 成功，false 失败
 */
bool txt_reader_seek(txt_reader_t *reader, long position);

/**
 * @brief 获取当前阅读位置
 * @param reader 阅读器实例指针
 * @return 当前位置信息
 */
txt_position_t txt_reader_get_position(const txt_reader_t *reader);

/**
 * @brief 获取文件总页数（估算）
 * @param reader 阅读器实例指针
 * @param chars_per_page 每页字符数
 * @return 总页数
 */
int txt_reader_get_total_pages(const txt_reader_t *reader, int chars_per_page);

/**
 * @brief 保存阅读位置到 NVS
 * @param reader 阅读器实例指针
 * @return true 成功，false 失败
 */
bool txt_reader_save_position(const txt_reader_t *reader);

/**
 * @brief 从 NVS 加载阅读位置
 * @param reader 阅读器实例指针
 * @return true 成功且已跳转，false 无保存位置或失败
 */
bool txt_reader_load_position(txt_reader_t *reader);

/**
 * @brief 检测文件编码
 * @param file_path 文件路径
 * @return 检测到的编码类型
 */
txt_encoding_t txt_reader_detect_encoding(const char *file_path);

/**
 * @brief 清理阅读器资源
 * @param reader 阅读器实例指针
 */
void txt_reader_cleanup(txt_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif // TXT_READER_H
