/**
 * @file epub_zip.h
 * @brief 轻量级 EPUB ZIP 解析器 - 流式解压，节省内存
 *
 * 基于 miniz，实现按需解压 EPUB 内的文件
 * 不一次性解压整个 EPUB，只解压需要的文件
 */

#ifndef EPUB_ZIP_H
#define EPUB_ZIP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ZIP 内文件信息
typedef struct {
    char filename[256];    // 文件名（在 ZIP 内的路径）
    uint32_t offset;       // 文件数据在 ZIP 中的偏移
    uint32_t compressed_size;   // 压缩大小
    uint32_t uncompressed_size; // 解压大小
    uint16_t compression_method; // 压缩方法 (0=存储, 8=deflate)
} epub_zip_file_info_t;

/**
 * @brief 打开 EPUB 文件（ZIP 格式）
 * @param path EPUB 文件路径
 * @return ZIP 句柄，失败返回 NULL
 */
typedef struct epub_zip epub_zip_t;
epub_zip_t* epub_zip_open(const char *path);

/**
 * @brief 关闭 EPUB 文件
 * @param zip ZIP 句柄
 */
void epub_zip_close(epub_zip_t *zip);

/**
 * @brief 流式解压单个文件到缓冲区
 * @param zip ZIP 句柄
 * @param file_info 文件信息
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 实际解压的字节数，失败返回 -1
 */
int epub_zip_extract_file(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                          void *buffer, size_t buffer_size);

/**
 * @brief 流式解压单个文件到指定路径
 * @param zip ZIP 句柄
 * @param file_info 文件信息
 * @param out_path 输出文件路径
 * @return 实际解压的字节数，失败返回 -1
 */
int epub_zip_extract_file_to_path(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                                 const char *out_path);

/**
 * @brief 查找 ZIP 内的文件
 * @param zip ZIP 句柄
 * @param filename 文件名（如 "OEBPS/content.opf"）
 * @return 文件信息指针，未找到返回 NULL
 */
const epub_zip_file_info_t *epub_zip_find_file(epub_zip_t *zip, const char *filename);

/**
 * @brief 获取文件列表
 * @param zip ZIP 句柄
 * @param out_count 输出文件数量
 * @return 文件信息数组
 */
const epub_zip_file_info_t *epub_zip_get_files(epub_zip_t *zip, int *out_count);

#ifdef __cplusplus
}
#endif

#endif // EPUB_ZIP_H
