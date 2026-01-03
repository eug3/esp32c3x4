/**
 * @file epub_zip.c
 * @brief EPUB ZIP 解析器实现 - 简化版，使用 ESP-IDF 的 miniz
 */

#include "epub_zip.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

// ESP-IDF ROM 中包含 miniz，但多文件支持被禁用
// 这里使用简化版本：只读取 ZIP 中心目录，按需解压

static const char *TAG = "EPUB_ZIP";

// ZIP 文件头结构（简化）
#define ZIP_LOCAL_FILE_HEADER_SIGNATURE 0x04034b50
#define ZIP_CENTRAL_DIR_SIGNATURE 0x02014b50
#define ZIP_END_CENTRAL_DIR_SIGNATURE 0x06054b50

#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
} zip_local_file_header_t;

typedef struct {
    uint32_t signature;
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
    uint32_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_header_offset;
} zip_central_dir_entry_t;

typedef struct {
    uint32_t signature;
    uint16_t disk_num;
    uint16_t central_dir_disk;
    uint16_t disk_entries;
    uint16_t total_entries;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_len;
} zip_end_central_dir_t;
#pragma pack(pop)

struct epub_zip {
    FILE *file;
    char path[256];
    zip_end_central_dir_t end_record;
    epub_zip_file_info_t *file_list;
    int file_count;
};

// 查找并读取 ZIP 中心目录结束记录
static bool read_end_central_dir(FILE *file, zip_end_central_dir_t *end_record) {
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);

    // 注释最长 65535，从文件末尾最多向前搜索 64KB
    long max_search = file_size > 65536 ? 65536 : file_size;
    long search_start = file_size - max_search;

    uint8_t buffer[65536];
    long read_size = max_search;
    fseek(file, search_start, SEEK_SET);
    size_t n = fread(buffer, 1, read_size, file);

    // 从后向前搜索签名
    for (int i = n - sizeof(zip_end_central_dir_t); i >= 0; i--) {
        if (buffer[i] == 0x50 && buffer[i+1] == 0x4b &&
            buffer[i+2] == 0x05 && buffer[i+3] == 0x06) {
            memcpy(end_record, &buffer[i], sizeof(zip_end_central_dir_t));
            return true;
        }
    }

    return false;
}

// 读取中心目录并构建文件列表
static bool build_file_list(epub_zip_t *zip) {
    FILE *file = zip->file;
    fseek(file, zip->end_record.central_dir_offset, SEEK_SET);

    zip->file_list = calloc(zip->end_record.total_entries, sizeof(epub_zip_file_info_t));
    if (!zip->file_list) {
        ESP_LOGE(TAG, "Failed to allocate file list");
        return false;
    }

    int count = 0;
    for (int i = 0; i < zip->end_record.total_entries; i++) {
        zip_central_dir_entry_t entry;
        size_t n = fread(&entry, 1, sizeof(entry), file);
        if (n != sizeof(entry)) {
            break;
        }

        if (entry.signature != ZIP_CENTRAL_DIR_SIGNATURE) {
            ESP_LOGE(TAG, "Invalid central dir signature");
            break;
        }

        // 读取文件名
        if (entry.filename_len > 0 && entry.filename_len < 255) {
            epub_zip_file_info_t *info = &zip->file_list[count];
            fread(info->filename, 1, entry.filename_len, file);
            info->filename[entry.filename_len] = '\0';

            info->offset = entry.local_header_offset;
            info->compressed_size = entry.compressed_size;
            info->uncompressed_size = entry.uncompressed_size;
            info->compression_method = entry.compression;

            count++;
        }

        // 跳过 extra 和 comment
        fseek(file, entry.extra_len + entry.comment_len, SEEK_CUR);
    }

    zip->file_count = count;
    ESP_LOGI(TAG, "Built file list: %d files", count);
    return true;
}

epub_zip_t* epub_zip_open(const char *epub_path) {
    epub_zip_t *zip = calloc(1, sizeof(epub_zip_t));
    if (!zip) {
        ESP_LOGE(TAG, "Failed to allocate epub_zip");
        return NULL;
    }

    strncpy(zip->path, epub_path, sizeof(zip->path) - 1);

    zip->file = fopen(epub_path, "rb");
    if (!zip->file) {
        ESP_LOGE(TAG, "Failed to open EPUB: %s", epub_path);
        free(zip);
        return NULL;
    }

    // 读取中心目录
    if (!read_end_central_dir(zip->file, &zip->end_record)) {
        ESP_LOGE(TAG, "Failed to read end central dir");
        fclose(zip->file);
        free(zip);
        return NULL;
    }

    ESP_LOGI(TAG, "ZIP: %d entries, central dir at offset %u",
             zip->end_record.total_entries, zip->end_record.central_dir_offset);

    // 构建文件列表
    if (!build_file_list(zip)) {
        fclose(zip->file);
        free(zip);
        return NULL;
    }

    return zip;
}

void epub_zip_close(epub_zip_t *zip) {
    if (!zip) return;

    if (zip->file) {
        fclose(zip->file);
    }
    if (zip->file_list) {
        free(zip->file_list);
    }
    free(zip);
}

int epub_zip_list_files(epub_zip_t *zip, const char *pattern,
                        epub_zip_file_info_t *files, int max_files) {
    if (!zip || !files || max_files <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < zip->file_count && count < max_files; i++) {
        if (!pattern || strstr(zip->file_list[i].filename, pattern)) {
            memcpy(&files[count], &zip->file_list[i], sizeof(epub_zip_file_info_t));
            count++;
        }
    }

    return count;
}

bool epub_zip_find_file(epub_zip_t *zip, const char *filename,
                        epub_zip_file_info_t *file_info) {
    if (!zip || !filename || !file_info) {
        return false;
    }

    for (int i = 0; i < zip->file_count; i++) {
        if (strcmp(zip->file_list[i].filename, filename) == 0 ||
            strstr(zip->file_list[i].filename, filename)) {
            memcpy(file_info, &zip->file_list[i], sizeof(epub_zip_file_info_t));
            return true;
        }
    }

    return false;
}

int epub_zip_extract_file(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                          void *buffer, size_t buffer_size) {
    if (!zip || !file_info || !buffer) {
        return -1;
    }

    if (file_info->uncompressed_size > buffer_size) {
        ESP_LOGE(TAG, "Buffer too small: need %u, have %u",
                 file_info->uncompressed_size, buffer_size);
        return -1;
    }

    // 跳到本地文件头
    fseek(zip->file, file_info->offset, SEEK_SET);

    // 读取本地文件头
    zip_local_file_header_t local_header;
    if (fread(&local_header, 1, sizeof(local_header), zip->file) != sizeof(local_header)) {
        ESP_LOGE(TAG, "Failed to read local header");
        return -1;
    }

    if (local_header.signature != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        ESP_LOGE(TAG, "Invalid local header signature");
        return -1;
    }

    // 跳过文件名和 extra
    fseek(zip->file, local_header.filename_len + local_header.extra_len, SEEK_CUR);

    // 读取数据
    if (local_header.compression == 0) {
        // 存储 - 直接读取
        size_t n = fread(buffer, 1, local_header.uncompressed_size, zip->file);
        return n;
    } else if (local_header.compression == 8) {
        // Deflate - 需要 miniz 解压
        ESP_LOGW(TAG, "Deflate compression not implemented yet");
        return -1;
    }

    ESP_LOGE(TAG, "Unsupported compression method: %u", local_header.compression);
    return -1;
}

int epub_zip_get_file_count(epub_zip_t *zip) {
    return zip ? zip->file_count : 0;
}
