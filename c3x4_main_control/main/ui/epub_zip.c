/**
 * @file epub_zip.c
 * @brief EPUB ZIP 解析器实现 - 简化版，使用 ESP-IDF 的 miniz
 */

#include "epub_zip.h"
#include "esp_log.h"
#include "zlib.h"
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

    // 说明：阅读器章节缓冲区可能较小（例如 4KB）。
    // 这里允许“截断式解压/读取”，返回实际写入 buffer 的字节数。
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
        ESP_LOGE(TAG, "Invalid local header signature");
        return -1;
    }

    // 跳过文件名和 extra
    fseek(zip->file, local_header.filename_len + local_header.extra_len, SEEK_CUR);

    // 读取数据
    if (local_header.compression == 0) {
        // 存储 - 直接读取
        size_t to_read = (size_t)file_info->compressed_size;
        if (to_read > buffer_size) {
            to_read = buffer_size;
        }
        size_t n = fread(buffer, 1, to_read, zip->file);
        return (int)n;
    } else if (local_header.compression == 8) {
        // Deflate (raw) - ZIP 使用 raw deflate，无 zlib 头
        // 这里使用工程内的 zlib(inflate) 实现（PNGdec 依赖）进行流式解压。
        const size_t compressed_total = (size_t)file_info->compressed_size;
        const size_t uncompressed_total = (size_t)file_info->uncompressed_size;
        const size_t out_limit = (uncompressed_total > buffer_size) ? buffer_size : uncompressed_total;

        // 小块输入，避免大内存/大栈
        const size_t CHUNK = 4096;
        uint8_t *in_chunk = (uint8_t *)malloc(CHUNK);
        if (in_chunk == NULL) {
            ESP_LOGE(TAG, "Failed to allocate deflate input chunk");
            return -1;
        }

        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = inflateInit2(&strm, -15);
        if (ret != Z_OK) {
            ESP_LOGE(TAG, "inflateInit2 failed: %d", ret);
            free(in_chunk);
            return -1;
        }

        strm.next_out = (Bytef *)buffer;
        strm.avail_out = (uInt)out_limit;

        size_t remaining = compressed_total;
        size_t in_have = 0;

        while (1) {
            if (strm.avail_in == 0) {
                if (remaining == 0) {
                    // 没有更多输入了
                    in_have = 0;
                } else {
                    size_t to_read = remaining > CHUNK ? CHUNK : remaining;
                    size_t nr = fread(in_chunk, 1, to_read, zip->file);
                    if (nr == 0) {
                        ESP_LOGE(TAG, "Failed to read compressed data (remaining=%u)", (unsigned)remaining);
                        inflateEnd(&strm);
                        free(in_chunk);
                        return -1;
                    }
                    remaining -= nr;
                    in_have = nr;
                }

                strm.next_in = (z_const Bytef *)in_chunk;
                strm.avail_in = (uInt)in_have;
            }

            int flush = (remaining == 0 && strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
            ret = inflate(&strm, flush, 0);

            if (ret == Z_STREAM_END) {
                break;
            }

            if (ret != Z_OK && ret != Z_BUF_ERROR) {
                ESP_LOGE(TAG, "inflate failed: %d", ret);
                inflateEnd(&strm);
                free(in_chunk);
                return -1;
            }

            if (strm.avail_out == 0 && ret != Z_STREAM_END) {
                // 输出缓冲满：允许截断返回
                break;
            }

            // 防止死循环：没有输入、没有输出进展且已到输入末尾
            if (remaining == 0 && strm.avail_in == 0 && ret == Z_BUF_ERROR) {
                ESP_LOGE(TAG, "inflate stalled (no input remaining)");
                inflateEnd(&strm);
                free(in_chunk);
                return -1;
            }
        }

        ret = inflateEnd(&strm);
        free(in_chunk);

        if (ret != Z_OK) {
            ESP_LOGW(TAG, "inflateEnd returned %d", ret);
        }

        if ((size_t)strm.total_out != uncompressed_total && out_limit == uncompressed_total) {
            // 未截断时，通常应一致
            ESP_LOGW(TAG, "Inflate size mismatch: out=%u expected=%u",
                     (unsigned)strm.total_out, (unsigned)uncompressed_total);
        }

        return (int)strm.total_out;
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

    const size_t CHUNK = 4096;
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
        // Deflate(raw)
        z_stream strm;
        memset(&strm, 0, sizeof(strm));

        int ret = inflateInit2(&strm, -15);
        if (ret != Z_OK) {
            ESP_LOGE(TAG, "inflateInit2 failed: %d", ret);
            written_total = -1;
        } else {
            size_t remaining = (size_t)file_info->compressed_size;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;

            while (1) {
                if (strm.avail_in == 0 && remaining > 0) {
                    size_t to_read = remaining > CHUNK ? CHUNK : remaining;
                    size_t nr = fread(in_chunk, 1, to_read, zip->file);
                    if (nr == 0) {
                        ESP_LOGE(TAG, "Failed to read deflate data (remaining=%u)", (unsigned)remaining);
                        written_total = -1;
                        break;
                    }
                    remaining -= nr;
                    strm.next_in = (z_const Bytef *)in_chunk;
                    strm.avail_in = (uInt)nr;
                }

                strm.next_out = (Bytef *)out_chunk;
                strm.avail_out = (uInt)CHUNK;

                int flush = (remaining == 0 && strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
                ret = inflate(&strm, flush, 0);

                if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                    ESP_LOGE(TAG, "inflate failed: %d", ret);
                    written_total = -1;
                    break;
                }

                size_t produced = CHUNK - (size_t)strm.avail_out;
                if (produced > 0) {
                    size_t nw = fwrite(out_chunk, 1, produced, out);
                    if (nw != produced) {
                        ESP_LOGE(TAG, "Failed to write inflated data");
                        written_total = -1;
                        break;
                    }
                    written_total += (int)produced;
                }

                if (ret == Z_STREAM_END) {
                    break;
                }

                if (remaining == 0 && strm.avail_in == 0 && ret == Z_BUF_ERROR) {
                    ESP_LOGE(TAG, "inflate stalled (no input remaining)");
                    written_total = -1;
                    break;
                }
            }

            (void)inflateEnd(&strm);
        }
    } else {
        ESP_LOGE(TAG, "Unsupported compression method: %u", local_header.compression);
        written_total = -1;
    }

    free(in_chunk);
    free(out_chunk);
    fclose(out);

    return written_total;
}
