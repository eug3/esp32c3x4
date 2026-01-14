/**
 * @file epub_zip.c
 * @brief EPUB ZIP 解析器实现 - 优化版，使用改进的 tinfl 流式解压
 * 关键改进：使用 central directory 中的准确大小信息，而不是 local header
 */

#include "epub_zip.h"
#include "esp_log.h"
#include "esp_system.h"

#include "miniz.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>

static const char *TAG = "EPUB_ZIP";

// EPUB ZIP 文件结构体定义
typedef struct epub_zip {
    FILE *file;
    char path[256];
    int file_count;
    epub_zip_file_info_t *files;
} epub_zip_t;

// ZIP 本地文件头结构 (30 字节固定部分)
typedef struct __attribute__((packed)) {
    uint32_t signature;           // 0x04034b50
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;              // 如果 flags & 0x08，则为 0
    uint32_t compressed_size;    // 如果 flags & 0x08，则为 0
    uint32_t uncompressed_size;  // 如果 flags & 0x08，则为 0
    uint16_t filename_len;
    uint16_t extra_len;
    // 后跟 filename_len + extra_len 字节
} zip_local_file_header_t;

#define ZIP_LOCAL_FILE_HEADER_SIGNATURE 0x04034b50

// ZIP 中心目录头结构 (46 字节固定部分)
typedef struct __attribute__((packed)) {
    uint32_t signature;           // 0x02014b50
    uint16_t version_made;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_header_offset;
    // 后跟 filename_len + extra_len + comment_len 字节
} zip_central_directory_header_t;

#define ZIP_CENTRAL_DIRECTORY_SIGNATURE 0x02014b50

// 结尾中心目录记录 (22 字节固定部分)
typedef struct __attribute__((packed)) {
    uint32_t signature;           // 0x06054b50
    uint16_t disk_num;
    uint16_t disk_with_central;
    uint16_t entries_on_disk;
    uint16_t total_entries;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_len;
} zip_end_central_directory_t;

_Static_assert(sizeof(zip_local_file_header_t) == 30, "zip_local_file_header_t must be packed (30 bytes)");
_Static_assert(sizeof(zip_central_directory_header_t) == 46, "zip_central_directory_header_t must be packed (46 bytes)");
_Static_assert(sizeof(zip_end_central_directory_t) == 22, "zip_end_central_directory_t must be packed (22 bytes)");

#define ZIP_END_CENTRAL_DIRECTORY_SIGNATURE 0x06054b50

// 前向声明
static int epub_zip_extract_deflate(epub_zip_t *zip, void *buffer, size_t buffer_size,
                                    uint32_t compressed_total, uint32_t uncompressed_total);

/**
 * 打开 ZIP 文件并读取文件列表
 */
epub_zip_t *epub_zip_open(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open ZIP: %s", path);
        return NULL;
    }

    // 分配结构体
    epub_zip_t *zip = (epub_zip_t *)malloc(sizeof(epub_zip_t));
    if (zip == NULL) {
        fclose(file);
        return NULL;
    }

    zip->file = file;
    strncpy(zip->path, path, sizeof(zip->path) - 1);
    zip->path[sizeof(zip->path) - 1] = '\0';
    zip->file_count = 0;
    zip->files = NULL;

    // 查找 end of central directory
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    
    if (file_size < (long)sizeof(zip_end_central_directory_t)) {
        ESP_LOGE(TAG, "File too small");
        free(zip);
        fclose(file);
        return NULL;
    }

    // 快速定位 End of Central Directory (EOCD): 读取末尾最多 66KB 到内存中一次性搜索，避免逐字节 fseek/fread 过慢
    zip_end_central_directory_t end_dir = {0};
    ESP_LOGI(TAG, "zip_open: searching end of central directory (size=%ld)...", file_size);

    size_t window = (file_size < 66000) ? (size_t)file_size : 66000; // 64KB + 2KB 余量
    size_t read_offset = (size_t)file_size - window;
    uint8_t *buf = (uint8_t *)malloc(window);
    if (!buf) {
        ESP_LOGE(TAG, "zip_open: OOM while allocating search buffer");
        free(zip);
        fclose(file);
        return NULL;
    }

    fseek(file, read_offset, SEEK_SET);
    size_t nread = fread(buf, 1, window, file);
    if (nread != window) {
        ESP_LOGE(TAG, "zip_open: failed to read tail window (%u/%u)", (unsigned)nread, (unsigned)window);
        free(buf);
        free(zip);
        fclose(file);
        return NULL;
    }

    long found_pos = -1;
    for (long i = (long)window - 4; i >= 0; i--) {
        if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 0x05 && buf[i + 3] == 0x06) {
            found_pos = (long)read_offset + i;
            break;
        }
    }
    free(buf);

    if (found_pos >= 0) {
        fseek(file, found_pos, SEEK_SET);
        if (fread(&end_dir, 1, sizeof(end_dir), file) == sizeof(end_dir)) {
            ESP_LOGI(TAG, "zip_open: found end of central dir at offset %ld (entries=%u, dir_size=%u, dir_offset=%u)",
                     found_pos, end_dir.total_entries, end_dir.central_dir_size, end_dir.central_dir_offset);
        }
    }

    if (end_dir.signature != ZIP_END_CENTRAL_DIRECTORY_SIGNATURE) {
        ESP_LOGE(TAG, "Invalid ZIP: no end of central directory found");
        free(zip);
        fclose(file);
        return NULL;
    }

    // 分配文件信息数组
    zip->file_count = end_dir.total_entries;
    zip->files = (epub_zip_file_info_t *)malloc(sizeof(epub_zip_file_info_t) * zip->file_count);
    if (zip->files == NULL) {
        ESP_LOGE(TAG, "Failed to allocate file list");
        free(zip);
        fclose(file);
        return NULL;
    }

    // 读取所有中心目录条目（严格按照 central_dir_size 边界，防止错位）
    uint32_t remaining = end_dir.central_dir_size;
    fseek(file, end_dir.central_dir_offset, SEEK_SET);
    for (int i = 0; i < end_dir.total_entries; i++) {
        if (remaining < sizeof(zip_central_directory_header_t)) {
            ESP_LOGE(TAG, "Central directory truncated before entry %d (remaining=%u)", i, remaining);
            epub_zip_close(zip);
            return NULL;
        }

        zip_central_directory_header_t header;
        if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
            ESP_LOGE(TAG, "Failed to read central directory entry %d", i);
            epub_zip_close(zip);
            return NULL;
        }
        remaining -= sizeof(header);

        if (header.signature != ZIP_CENTRAL_DIRECTORY_SIGNATURE) {
            long bad_off = ftell(file) - (long)sizeof(header);
            ESP_LOGE(TAG, "Invalid central directory entry signature at entry %d (offset=%ld, remaining=%u)", i, bad_off, remaining);
            epub_zip_close(zip);
            return NULL;
        }

        // 读取文件名（只保留前 255 字符，剩余部分跳过，避免指针对不齐）
        char filename[256];
        size_t name_len = (header.filename_len > 255) ? 255 : header.filename_len;
        if (remaining < header.filename_len) {
            ESP_LOGE(TAG, "Central directory truncated in filename at entry %d (need=%u, remaining=%u)", i, header.filename_len, remaining);
            epub_zip_close(zip);
            return NULL;
        }
        if (fread(filename, 1, name_len, file) != name_len) {
            ESP_LOGE(TAG, "Failed to read filename (entry %d)", i);
            epub_zip_close(zip);
            return NULL;
        }
        filename[name_len] = '\0';
        if (header.filename_len > name_len) {
            fseek(file, header.filename_len - name_len, SEEK_CUR);
        }
        remaining -= header.filename_len;

        // 保存文件信息
        epub_zip_file_info_t *file_info = &zip->files[i];
        strncpy(file_info->filename, filename, sizeof(file_info->filename) - 1);
        file_info->filename[sizeof(file_info->filename) - 1] = '\0';
        file_info->offset = header.local_header_offset;
        file_info->compressed_size = header.compressed_size;
        file_info->uncompressed_size = header.uncompressed_size;
        file_info->compression_method = header.compression;

        ESP_LOGV(TAG, "File %d: %s offset=%u comp=%u uncomp=%u method=%u",
                 i, filename, header.local_header_offset,
                 header.compressed_size, header.uncompressed_size,
                 header.compression);

        // 跳过 extra 和注释
        uint32_t skip = header.extra_len + header.comment_len;
        if (remaining < skip) {
            ESP_LOGE(TAG, "Central directory truncated in extra/comment at entry %d (need=%u, remaining=%u)", i, skip, remaining);
            epub_zip_close(zip);
            return NULL;
        }
        fseek(file, skip, SEEK_CUR);
        remaining -= skip;
    }

    ESP_LOGI(TAG, "Opened ZIP: %s (%d files)", path, zip->file_count);
    return zip;
}

/**
 * 关闭 ZIP 文件
 */
void epub_zip_close(epub_zip_t *zip) {
    if (zip == NULL) {
        return;
    }
    if (zip->file != NULL) {
        fclose(zip->file);
    }
    if (zip->files != NULL) {
        free(zip->files);
    }
    free(zip);
}

/**
 * 获取文件列表
 */
const epub_zip_file_info_t *epub_zip_get_files(epub_zip_t *zip, int *out_count) {
    if (zip == NULL) {
        *out_count = 0;
        return NULL;
    }
    *out_count = zip->file_count;
    return zip->files;
}

/**
 * 查找指定名称的文件
 */
const epub_zip_file_info_t *epub_zip_find_file(epub_zip_t *zip, const char *filename) {
    if (zip == NULL || filename == NULL) {
        return NULL;
    }

    for (int i = 0; i < zip->file_count; i++) {
        if (strcmp(zip->files[i].filename, filename) == 0) {
            return &zip->files[i];
        }
    }

    return NULL;
}

/**
 * Deflate 解压辅助函数
 * 关键特点：正确处理 HAS_MORE_INPUT 标志和压缩字节追踪
 */
static int epub_zip_extract_deflate(epub_zip_t *zip, void *buffer, size_t buffer_size,
                                    uint32_t compressed_total, uint32_t uncompressed_total) {
    ESP_LOGI(TAG, "Deflate start: comp_total=%u, uncomp_total=%u, buf=%u",
             (unsigned)compressed_total, (unsigned)uncompressed_total, (unsigned)buffer_size);

    const size_t IN_CHUNK = 4096;
    uint8_t *in_chunk = (uint8_t *)malloc(IN_CHUNK);
    if (!in_chunk) {
        return -1;
    }

    tinfl_decompressor decomp;
    tinfl_init(&decomp);

    size_t total_written = 0;
    size_t comp_remaining = compressed_total;
    const size_t out_limit = (uncompressed_total > buffer_size) ? buffer_size : uncompressed_total;

    const mz_uint8 *next_in = in_chunk;
    size_t avail_in = 0;
    tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

    // 直接解压到目标缓冲区，避免额外的 memcpy
    mz_uint8 *out_buf_start = (mz_uint8 *)buffer;
    mz_uint8 *out_buf_cur = out_buf_start;

    while (total_written < out_limit) {
        if ((avail_in == 0) && (comp_remaining > 0) && (status == TINFL_STATUS_NEEDS_MORE_INPUT)) {
            size_t to_read = (comp_remaining > IN_CHUNK) ? IN_CHUNK : comp_remaining;
            size_t nr = fread(in_chunk, 1, to_read, zip->file);
            if (nr == 0) {
                ESP_LOGE(TAG, "Failed to read compressed data (remaining=%u)", (unsigned)comp_remaining);
                free(in_chunk);
                return -1;
            }
            next_in = in_chunk;
            avail_in = nr;
            comp_remaining -= nr;
        }

        size_t in_bytes = avail_in;
        size_t out_bytes = out_limit;  // 从 out_buf_start 到缓冲区末尾的总大小
        mz_uint flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
        if (comp_remaining > 0 || avail_in > 0) {
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
        }

        status = tinfl_decompress(&decomp, next_in, &in_bytes,
                                  out_buf_start, out_buf_cur, &out_bytes, flags);

        if (status < 0) {
            ESP_LOGE(TAG, "tinfl failed with status %d", (int)status);
            free(in_chunk);
            return -1;
        }

        // tinfl_decompress() 返回：in_bytes=消耗的输入字节数, out_bytes=产生的输出字节数
        if (in_bytes > avail_in) {
            ESP_LOGE(TAG, "tinfl consumed too much input (%u > %u)", (unsigned)in_bytes, (unsigned)avail_in);
            free(in_chunk);
            return -1;
        }
        avail_in -= in_bytes;
        next_in += in_bytes;

        if (out_bytes > 0) {
            out_buf_cur += out_bytes;
            total_written += out_bytes;

            if (total_written >= out_limit) {
                free(in_chunk);
                return (int)total_written;
            }
        }

        if (status == TINFL_STATUS_DONE) {
            ESP_LOGI(TAG, "Deflate decompression completed, total_written=%u", (unsigned)total_written);
            free(in_chunk);
            return (int)total_written;
        }

        if (status == TINFL_STATUS_DONE) {
            ESP_LOGI(TAG, "Deflate decompression completed, total_written=%u", (unsigned)total_written);
            free(in_chunk);
            return (int)total_written;
        }

        if (status != TINFL_STATUS_HAS_MORE_OUTPUT && status != TINFL_STATUS_NEEDS_MORE_INPUT) {
            ESP_LOGE(TAG, "Unexpected tinfl status: %d", status);
            free(in_chunk);
            return -1;
        }
    }

    ESP_LOGW(TAG, "Deflate ended early: wrote=%u, expected=%u", (unsigned)total_written, (unsigned)out_limit);
    free(in_chunk);
    return (int)total_written;
}

/**
 * 提取 ZIP 文件到内存缓冲区
 * 
 * 关键改进：
 * 1. 使用 central directory 中的准确大小信息（不依赖 local header）
 * 2. 改进 tinfl 流式解压逻辑，正确处理 HAS_MORE_INPUT 标志
 * 3. 正确追踪已读取的压缩字节数
 */
int epub_zip_extract_file(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                          void *buffer, size_t buffer_size) {
    if (!zip || !file_info || !buffer) {
        return -1;
    }

    ESP_LOGI(TAG, "extract_file: offset=%u, comp=%u, uncomp=%u, comp_method=%u",
             file_info->offset, file_info->compressed_size,
             file_info->uncompressed_size, file_info->compression_method);

    // 跳到本地文件头
    fseek(zip->file, file_info->offset, SEEK_SET);

    // 读取本地文件头的前固定部分
    zip_local_file_header_t local_header;
    if (fread(&local_header, 1, sizeof(local_header), zip->file) != sizeof(local_header)) {
        ESP_LOGE(TAG, "Failed to read local header");
        return -1;
    }

    if (local_header.signature != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        ESP_LOGE(TAG, "Invalid local header signature: 0x%08x", (unsigned)local_header.signature);
        return -1;
    }

    ESP_LOGI(TAG, "Local header: filename_len=%u, extra_len=%u, compression=%u, flags=0x%04x",
             local_header.filename_len, local_header.extra_len,
             local_header.compression, local_header.flags);

    // 关键：跳过文件名和 extra 字段以到达实际的压缩数据
    fseek(zip->file, local_header.filename_len + local_header.extra_len, SEEK_CUR);

    // 使用 central directory 中的准确大小信息
    // （local header 中的值在有数据描述符时为 0）
    uint32_t actual_compressed = file_info->compressed_size;
    uint32_t actual_uncompressed = file_info->uncompressed_size;
    uint16_t compression_method = file_info->compression_method;

    if (compression_method == 0) {
        // 存储方式 - 直接复制
        ESP_LOGI(TAG, "Stored compression - direct copy");
        size_t to_read = (size_t)actual_compressed;
        if (to_read > buffer_size) {
            to_read = buffer_size;
        }
        size_t n = fread(buffer, 1, to_read, zip->file);
        ESP_LOGI(TAG, "Stored copy done: read=%u", (unsigned)n);
        return (int)n;
    } else if (compression_method == 8) {
        // Deflate - 使用改进的 tinfl 流式解压
        int ret = epub_zip_extract_deflate(zip, buffer, buffer_size,
                                           actual_compressed, actual_uncompressed);
        ESP_LOGI(TAG, "Deflate done: wrote=%d", ret);
        return ret;
    }

    ESP_LOGE(TAG, "Unsupported compression method: %u", compression_method);
    return -1;
}

int epub_zip_get_file_count(epub_zip_t *zip) {
    return zip ? zip->file_count : 0;
}

int epub_zip_extract_file_to_path(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                                 const char *out_path) {
    if (zip == NULL || file_info == NULL || out_path == NULL || out_path[0] == '\0') {
        return -1;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        ESP_LOGE(TAG, "Failed to open output file: %s", out_path);
        return -1;
    }

    // 分配缓冲区来读取文件
    const size_t BUF_SIZE = 4096;
    uint8_t *buffer = (uint8_t *)malloc(BUF_SIZE);
    if (buffer == NULL) {
        fclose(out);
        return -1;
    }

    int bytes_read = epub_zip_extract_file(zip, file_info, buffer, BUF_SIZE);
    if (bytes_read < 0) {
        free(buffer);
        fclose(out);
        return -1;
    }

    // 写入文件
    size_t written = fwrite(buffer, 1, bytes_read, out);
    free(buffer);
    fclose(out);

    if (written != (size_t)bytes_read) {
        ESP_LOGE(TAG, "Failed to write all data to %s", out_path);
        return -1;
    }

    return bytes_read;
}
