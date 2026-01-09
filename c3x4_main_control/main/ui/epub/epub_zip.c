/**
 * @file epub_zip.c
 * @brief EPUB ZIP 解析器实现 - 简化版，使用 ESP-IDF 的 miniz
 */

#include "epub_zip.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

// 使用 ESP ROM 提供的 miniz(tinfl) 解压，而不是 pngdec 的修改版 zlib
#include "miniz.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

static const char *TAG = "EPUB_ZIP";

// ESP-IDF ROM 中包含 miniz，但多文件支持被禁用
// 这里使用简化版本：只读取 ZIP 中心目录，按需解压

// 不再需要 zlib 的自定义分配器

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
    uint16_t disk_start;
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

static bool skip_bytes(FILE *file, size_t n)
{
    if (file == NULL) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    // Prefer fseek (fast), fallback to fread if seek fails (some FS configs)
    if (fseek(file, (long)n, SEEK_CUR) == 0) {
        return true;
    }
    uint8_t tmp[128];
    size_t remaining = n;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(tmp) ? sizeof(tmp) : remaining;
        size_t r = fread(tmp, 1, chunk, file);
        if (r == 0) {
            return false;
        }
        remaining -= r;
    }
    return true;
}

// 查找并读取 ZIP 中心目录结束记录
static bool read_end_central_dir(FILE *file, zip_end_central_dir_t *end_record) {
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);

    if (file_size <= 0 || file_size < (long)sizeof(zip_end_central_dir_t)) {
        return false;
    }

    // 注释最长 65535，从文件末尾最多向前搜索 64KB
    long max_search = file_size > 65536 ? 65536 : file_size;
    long search_start = file_size - max_search;

    // 注意：这里不能用大栈数组（input_poll 任务栈较小）。改为堆分配。
    size_t read_size = (size_t)max_search;
    uint8_t *buffer = (uint8_t *)malloc(read_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for EOCD search", (unsigned)read_size);
        return false;
    }

    fseek(file, search_start, SEEK_SET);
    size_t n = fread(buffer, 1, read_size, file);
    if (n < sizeof(zip_end_central_dir_t)) {
        free(buffer);
        return false;
    }

    // 从后向前搜索签名
    for (int i = (int)(n - sizeof(zip_end_central_dir_t)); i >= 0; i--) {
        if (buffer[i] == 0x50 && buffer[i+1] == 0x4b &&
            buffer[i+2] == 0x05 && buffer[i+3] == 0x06) {
            memcpy(end_record, &buffer[i], sizeof(zip_end_central_dir_t));
            free(buffer);
            return true;
        }
    }

    free(buffer);

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

        // 读取/跳过文件名：必须始终消费 filename_len 字节，否则后续 entry 会错位
        epub_zip_file_info_t *info = NULL;
        bool store_name = (entry.filename_len > 0 && entry.filename_len < (uint16_t)sizeof(((epub_zip_file_info_t *)0)->filename));
        if (store_name && count < zip->end_record.total_entries) {
            info = &zip->file_list[count];
            size_t rn = fread(info->filename, 1, entry.filename_len, file);
            if (rn != entry.filename_len) {
                break;
            }
            info->filename[entry.filename_len] = '\0';

            info->offset = entry.local_header_offset;
            info->compressed_size = entry.compressed_size;
            info->uncompressed_size = entry.uncompressed_size;
            info->compression_method = entry.compression;

            count++;
        } else {
            // 不存储长文件名，但仍需跳过
            if (!skip_bytes(file, entry.filename_len)) {
                break;
            }
        }

        // 跳过 extra 和 comment
        if (!skip_bytes(file, (size_t)entry.extra_len + (size_t)entry.comment_len)) {
            break;
        }
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

    ESP_LOGI(TAG, "extract_file: offset=%u, comp=%u, uncomp=%u, comp_method=%u",
             file_info->offset, file_info->compressed_size,
             file_info->uncompressed_size, file_info->compression_method);

    // 说明：阅读器章节缓冲区可能较小（例如 4KB）。
    // 这里允许"截断式解压/读取"，返回实际写入 buffer 的字节数。
    if (file_info->uncompressed_size > buffer_size) {
        ESP_LOGW(TAG, "Buffer smaller than uncompressed size: need %u, have %u (will truncate)",
                 (unsigned)file_info->uncompressed_size, (unsigned)buffer_size);
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
        ESP_LOGE(TAG, "Invalid local header signature: 0x%08x", (unsigned)local_header.signature);
        return -1;
    }

    ESP_LOGI(TAG, "Local header: filename_len=%u, extra_len=%u, compression=%u, flags=0x%04x",
             local_header.filename_len, local_header.extra_len,
             local_header.compression, local_header.flags);

    // 跳过文件名和 extra
    fseek(zip->file, local_header.filename_len + local_header.extra_len, SEEK_CUR);

    // 读取数据
    if (local_header.compression == 0) {
        // 存储 - 直接读取
        ESP_LOGI(TAG, "Stored compression - direct copy");
        size_t to_read = (size_t)file_info->compressed_size;
        if (to_read > buffer_size) {
            to_read = buffer_size;
        }
        size_t n = fread(buffer, 1, to_read, zip->file);
        return (int)n;
    } else if (local_header.compression == 8) {
        // Deflate (raw) - 使用 ROM miniz 的 tinfl 流式解压
        const size_t compressed_total = (size_t)file_info->compressed_size;
        const size_t uncompressed_total = (size_t)file_info->uncompressed_size;
        const size_t out_limit = (uncompressed_total > buffer_size) ? buffer_size : uncompressed_total;

        const size_t IN_CHUNK = 1024;
        const size_t OUT_CHUNK = 1024;
        uint8_t *in_chunk = (uint8_t *)malloc(IN_CHUNK);
        uint8_t *out_chunk = (uint8_t *)malloc(OUT_CHUNK);
        if (!in_chunk || !out_chunk) {
            free(in_chunk);
            free(out_chunk);
            return -1;
        }

        tinfl_decompressor decomp;
        tinfl_init(&decomp);

        size_t total_written = 0;
        size_t remaining = compressed_total;
        tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

        while (total_written < out_limit) {
            size_t to_read = remaining > IN_CHUNK ? IN_CHUNK : remaining;
            size_t nr = (to_read > 0) ? fread(in_chunk, 1, to_read, zip->file) : 0;
            if (to_read > 0 && nr == 0) {
                ESP_LOGE(TAG, "Failed to read deflate data (remaining=%u)", (unsigned)remaining);
                free(in_chunk);
                free(out_chunk);
                return -1;
            }
            remaining -= nr;

            const mz_uint8 *pIn = in_chunk;
            size_t in_size = nr;

            while (1) {
                size_t out_size = OUT_CHUNK;
                status = tinfl_decompress(&decomp, pIn, &in_size, out_chunk, out_chunk, &out_size,
                                          (remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0));

                if (status < 0) {
                    ESP_LOGE(TAG, "tinfl failed: %d (flags=0x%x)", (int)status, 0);
                    free(in_chunk);
                    free(out_chunk);
                    return -1;
                }

                if (out_size > 0) {
                    size_t to_copy = out_size;
                    if (total_written + to_copy > out_limit) {
                        to_copy = out_limit - total_written;
                    }
                    memcpy((uint8_t *)buffer + total_written, out_chunk, to_copy);
                    total_written += to_copy;
                    if (total_written >= out_limit) {
                        break;
                    }
                }

                if (status == TINFL_STATUS_DONE) {
                    break;
                }

                if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
                    continue;
                }

                if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
                    break;
                }
            }

            if (status == TINFL_STATUS_DONE || total_written >= out_limit) {
                break;
            }

            if (remaining == 0 && in_size == 0 && status != TINFL_STATUS_DONE) {
                ESP_LOGE(TAG, "tinfl stalled: no input left but not done");
                free(in_chunk);
                free(out_chunk);
                return -1;
            }
        }

        free(in_chunk);
        free(out_chunk);
        return (int)total_written;
    }

    ESP_LOGE(TAG, "Unsupported compression method: %u", local_header.compression);
    return -1;
}

int epub_zip_get_file_count(epub_zip_t *zip) {
    return zip ? zip->file_count : 0;
}

int epub_zip_extract_file_to_path(epub_zip_t *zip, const epub_zip_file_info_t *file_info,
                                 const char *out_path)
{
    if (zip == NULL || file_info == NULL || out_path == NULL || out_path[0] == '\0') {
        return -1;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        ESP_LOGE(TAG, "Failed to open output file: %s", out_path);
        return -1;
    }

    // 跳到本地文件头
    fseek(zip->file, file_info->offset, SEEK_SET);

    zip_local_file_header_t local_header;
    if (fread(&local_header, 1, sizeof(local_header), zip->file) != sizeof(local_header)) {
        ESP_LOGE(TAG, "Failed to read local header");
        fclose(out);
        return -1;
    }
    if (local_header.signature != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
        ESP_LOGE(TAG, "Invalid local header signature");
        fclose(out);
        return -1;
    }

    // 跳过文件名和 extra
    fseek(zip->file, local_header.filename_len + local_header.extra_len, SEEK_CUR);

    const size_t CHUNK = 1024;
    uint8_t *in_chunk = (uint8_t *)malloc(CHUNK);
    uint8_t *out_chunk = (uint8_t *)malloc(CHUNK);
    if (in_chunk == NULL || out_chunk == NULL) {
        ESP_LOGE(TAG, "Failed to allocate chunk buffers");
        free(in_chunk);
        free(out_chunk);
        fclose(out);
        return -1;
    }

    int written_total = 0;

    if (local_header.compression == 0) {
        // Stored: 直接拷贝 compressed_size 字节（central dir 给的大小更可靠）
        size_t remaining = (size_t)file_info->compressed_size;
        while (remaining > 0) {
            size_t to_read = remaining > CHUNK ? CHUNK : remaining;
            size_t nr = fread(in_chunk, 1, to_read, zip->file);
            if (nr == 0) {
                ESP_LOGE(TAG, "Failed to read stored data");
                written_total = -1;
                break;
            }
            size_t nw = fwrite(in_chunk, 1, nr, out);
            if (nw != nr) {
                ESP_LOGE(TAG, "Failed to write output file");
                written_total = -1;
                break;
            }
            remaining -= nr;
            written_total += (int)nr;
        }
    } else if (local_header.compression == 8) {
        // Deflate(raw) 使用 tinfl 流式解压写入文件
        ESP_LOGI(TAG, "Starting inflate: compressed=%u, heap=%u",
                 (unsigned)file_info->compressed_size, (unsigned)esp_get_free_heap_size());

        const size_t IN_CHUNK = 1024;
        const size_t OUT_CHUNK = 1024;
        uint8_t *in_chunk = (uint8_t *)malloc(IN_CHUNK);
        uint8_t *out_chunk = (uint8_t *)malloc(OUT_CHUNK);
        if (in_chunk == NULL || out_chunk == NULL) {
            ESP_LOGE(TAG, "Failed to allocate chunk buffers");
            free(in_chunk);
            free(out_chunk);
            fclose(out);
            return -1;
        }

        tinfl_decompressor decomp;
        tinfl_init(&decomp);

        size_t remaining = (size_t)file_info->compressed_size;
        tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

        while (1) {
            size_t to_read = remaining > IN_CHUNK ? IN_CHUNK : remaining;
            size_t nr = (to_read > 0) ? fread(in_chunk, 1, to_read, zip->file) : 0;
            if (to_read > 0 && nr == 0) {
                ESP_LOGE(TAG, "Failed to read deflate data (remaining=%u)", (unsigned)remaining);
                written_total = -1;
                break;
            }
            remaining -= nr;

            const mz_uint8 *pIn = in_chunk;
            size_t in_size = nr;

            while (1) {
                size_t out_size = OUT_CHUNK;
                status = tinfl_decompress(&decomp, pIn, &in_size, out_chunk, out_chunk, &out_size,
                                          (remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0));

                if (status < 0) {
                    ESP_LOGE(TAG, "tinfl failed: %d", (int)status);
                    written_total = -1;
                    break;
                }

                if (out_size) {
                    size_t nw = fwrite(out_chunk, 1, out_size, out);
                    if (nw != out_size) {
                        ESP_LOGE(TAG, "Failed to write inflated data");
                        written_total = -1;
                        break;
                    }
                    written_total += (int)out_size;
                }

                if (status == TINFL_STATUS_DONE) {
                    break;
                }

                if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
                    // 继续拉取输出（不消耗更多输入）
                    continue;
                }

                if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
                    // 需要更多输入时，跳出内层，去读下一块输入
                    break;
                }
            }

            if (status == TINFL_STATUS_DONE || written_total < 0) {
                break;
            }

            if (remaining == 0 && in_size == 0 && status != TINFL_STATUS_DONE) {
                ESP_LOGE(TAG, "tinfl stalled: no input left");
                written_total = -1;
                break;
            }
        }

        free(in_chunk);
        free(out_chunk);
    } else {
        ESP_LOGE(TAG, "Unsupported compression method: %u", local_header.compression);
        written_total = -1;
    }

    fclose(out);

    return written_total;
}
