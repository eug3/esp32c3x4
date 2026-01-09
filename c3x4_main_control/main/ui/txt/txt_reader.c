/**
 * @file txt_reader.c
 * @brief TXT 文本文件阅读器实现
 */

#include "txt_reader.h"
#include "gb18030_conv.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

static const char *TAG = "TXT_READER";

// NVS 命名空间
#define NVS_NAMESPACE "reader_pos"
// 注意：ESP-IDF NVS key 最长 15 字符，且建议只用 [0-9A-Za-z_]
#define NVS_KEY_PREFIX "txt_"

static uint32_t fnv1a32_str(const char *s)
{
    // 32-bit FNV-1a
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;
    uint32_t h = FNV_OFFSET;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint32_t)(*p);
        h *= FNV_PRIME;
    }
    return h;
}

static void make_nvs_key_for_txt_path(const char *file_path, char *out_key, size_t out_key_size)
{
    if (out_key == NULL || out_key_size == 0) {
        return;
    }

    // 用全路径 hash，避免同名不同目录冲突；key 长度控制在 <= 15
    uint32_t h = fnv1a32_str(file_path);
    // e.g. "txt_89abcdef" (12 chars)
    (void)snprintf(out_key, out_key_size, "%s%08" PRIx32, NVS_KEY_PREFIX, h);
    out_key[out_key_size - 1] = '\0';
}

// 读取缓冲区大小
#define READ_BUFFER_SIZE 4096

// BOM 检测
static bool is_utf8_bom(FILE *file) {
    unsigned char bom[3];
    long pos = ftell(file);
    if (fread(bom, 1, 3, file) == 3) {
        fseek(file, pos, SEEK_SET);
        return (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF);
    }
    fseek(file, pos, SEEK_SET);
    return false;
}

// UTF-8 合法性校验（严格校验，包含 overlong/surrogate 范围）
static bool __attribute__((unused))
is_valid_utf8_buffer(const unsigned char *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = buf[i];
        if (c < 0x80) {
            i++;
            continue;
        }

        int seq_len = 0;
        if ((c & 0xE0) == 0xC0) {
            // 2-byte
            if (c < 0xC2) {
                return false;  // overlong
            }
            seq_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte
            seq_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte
            if (c > 0xF4) {
                return false;
            }
            seq_len = 4;
        } else {
            return false;
        }

        if (i + (size_t)seq_len > len) {
            return false;
        }

        // Check continuation bytes
        for (int j = 1; j < seq_len; j++) {
            if ((buf[i + j] & 0xC0) != 0x80) {
                return false;
            }
        }

        // Additional constraints
        if (seq_len == 3) {
            unsigned char c1 = buf[i + 1];
            // Overlong for 3-byte
            if (c == 0xE0 && c1 < 0xA0) {
                return false;
            }
            // UTF-16 surrogate halves U+D800..U+DFFF
            if (c == 0xED && c1 >= 0xA0) {
                return false;
            }
        } else if (seq_len == 4) {
            unsigned char c1 = buf[i + 1];
            // Overlong for 4-byte
            if (c == 0xF0 && c1 < 0x90) {
                return false;
            }
            // > U+10FFFF
            if (c == 0xF4 && c1 > 0x8F) {
                return false;
            }
        }

        i += (size_t)seq_len;
    }
    return true;
}

// 简单编码检测（启发式）
static txt_encoding_t detect_encoding_from_content(FILE *file) {
    unsigned char buffer[4096];
    long pos = ftell(file);
    size_t n = fread(buffer, 1, sizeof(buffer), file);
    fseek(file, pos, SEEK_SET);

    if (n == 0) {
        return TXT_ENCODING_ASCII;
    }

    // 检查是否为纯 ASCII
    bool is_ascii = true;
    for (size_t i = 0; i < n; i++) {
        if (buffer[i] > 0x7F) {
            is_ascii = false;
            break;
        }
    }
    if (is_ascii) {
        ESP_LOGI(TAG, "Encoding detection: ASCII (no high bytes)");
        return TXT_ENCODING_ASCII;
    }

    // 统计 UTF-8 合法字符的覆盖率（不再要求 100% 合法）
    size_t valid_utf8_bytes = 0;
    size_t idx = 0;
    while (idx < n) {
        unsigned char c = buffer[idx];
        if (c < 0x80) {
            valid_utf8_bytes++;
            idx++;
            continue;
        }

        int seq_len = 0;
        if ((c & 0xE0) == 0xC0) {
            if (c < 0xC2) { idx++; continue; }
            seq_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) { idx++; continue; }
            seq_len = 4;
        } else {
            idx++; continue;
        }

        if (idx + (size_t)seq_len > n) break;

        bool valid_seq = true;
        for (int j = 1; j < seq_len; j++) {
            if ((buffer[idx + j] & 0xC0) != 0x80) {
                valid_seq = false;
                break;
            }
        }
        if (!valid_seq) { idx++; continue; }

        if (seq_len == 3) {
            unsigned char c1 = buffer[idx + 1];
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) {
                idx++; continue;
            }
        } else if (seq_len == 4) {
            unsigned char c1 = buffer[idx + 1];
            if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 > 0x8F)) {
                idx++; continue;
            }
        }

        valid_utf8_bytes += (size_t)seq_len;
        idx += (size_t)seq_len;
    }

    float utf8_ratio = (float)valid_utf8_bytes / (float)n;
    ESP_LOGI(TAG, "Encoding detection: valid_utf8_bytes=%u / total=%u (%.1f%%)",
             (unsigned)valid_utf8_bytes, (unsigned)n, utf8_ratio * 100.0f);

    if (utf8_ratio >= 0.80f) {
        ESP_LOGI(TAG, "Encoding detection: UTF-8 (ratio >= 80%%)");
        return TXT_ENCODING_UTF8;
    }

    // 再判断是否有 GB18030/GBK 特征：第一个字节在 0x81-0xFE 之间
    // 且第二个字节在 0x40-0xFE 之间（排除 0x7F）
    size_t gb_pairs = 0;
    for (size_t j = 0; j + 1 < n; j++) {
        if (buffer[j] >= 0x81 && buffer[j] <= 0xFE) {
            unsigned char next = buffer[j + 1];
            if (next >= 0x40 && next <= 0xFE && next != 0x7F) {
                gb_pairs++;
            }
        }
    }
    float gb_ratio = (float)(gb_pairs * 2) / (float)n;
    ESP_LOGI(TAG, "Encoding detection: gb_pairs=%u (%.1f%% coverage)",
             (unsigned)gb_pairs, gb_ratio * 100.0f);

    if (gb_pairs > 10 && gb_ratio > 0.30f) {
        ESP_LOGI(TAG, "Encoding detection: GB18030 (gb_pairs > 10 && ratio > 30%%)");
        return TXT_ENCODING_GB18030;
    }

    // 否则回退为 UTF-8（即使不完全合法也比乱转更安全）
    ESP_LOGI(TAG, "Encoding detection: fallback to UTF-8");
    return TXT_ENCODING_UTF8;
}

bool txt_reader_init(txt_reader_t *reader) {
    if (reader == NULL) {
        ESP_LOGE(TAG, "Invalid reader pointer");
        return false;
    }

    memset(reader, 0, sizeof(txt_reader_t));
    reader->buffer_size = READ_BUFFER_SIZE;

    reader->buffer = malloc(READ_BUFFER_SIZE);
    if (reader->buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return false;
    }

    ESP_LOGI(TAG, "TXT reader initialized");
    return true;
}

bool txt_reader_open(txt_reader_t *reader, const char *file_path, txt_encoding_t encoding) {
    if (reader == NULL || file_path == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    if (reader->is_open) {
        txt_reader_close(reader);
    }

    strncpy(reader->file_path, file_path, sizeof(reader->file_path) - 1);
    reader->file_path[sizeof(reader->file_path) - 1] = '\0';

    reader->file = fopen(file_path, "rb");
    if (reader->file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return false;
    }

    // 检测编码
    if (encoding == TXT_ENCODING_AUTO) {
        reader->encoding = txt_reader_detect_encoding(file_path);
    } else {
        reader->encoding = encoding;
    }

    // 获取文件大小
    fseek(reader->file, 0, SEEK_END);
    reader->position.file_size = ftell(reader->file);
    fseek(reader->file, 0, SEEK_SET);

    // 跳过 UTF-8 BOM
    if (reader->encoding == TXT_ENCODING_UTF8 && is_utf8_bom(reader->file)) {
        fseek(reader->file, 3, SEEK_SET);
    }

    reader->is_open = true;
    // 让逻辑位置与真实文件指针一致（BOM 场景下 ftell() 可能为 3）
    reader->position.file_position = ftell(reader->file);
    reader->position.page_number = 0;

    ESP_LOGI(TAG, "Opened TXT file: %s (encoding=%d, size=%ld bytes)",
             file_path, reader->encoding, reader->position.file_size);

    return true;
}

void txt_reader_close(txt_reader_t *reader) {
    if (reader == NULL) {
        return;
    }

    if (reader->file != NULL) {
        fclose(reader->file);
        reader->file = NULL;
    }

    reader->is_open = false;
    ESP_LOGI(TAG, "TXT reader closed");
}

int txt_reader_read_page(txt_reader_t *reader, char *text_buffer, size_t buffer_size, int chars_per_page) {
    if (reader == NULL || !reader->is_open || text_buffer == NULL) {
        ESP_LOGE(TAG, "Invalid reader or buffer");
        return -1;
    }

    if (buffer_size < 2) {
        ESP_LOGE(TAG, "Buffer too small");
        return -1;
    }

    text_buffer[0] = '\0';

    // Read raw bytes from file first
    uint8_t *raw_buffer = reader->buffer;
    size_t raw_bytes_read = 0;
    int chars_count = 0;

    // Read characters based on encoding
    if (reader->encoding == TXT_ENCODING_GB18030) {
        // GB18030 mode: read GB characters and convert to UTF-8
        while (chars_count < chars_per_page && raw_bytes_read < reader->buffer_size - 2) {
            int c = fgetc(reader->file);

            if (c == EOF) {
                break;
            }

            reader->position.file_position++;

            // Handle CR (skip)
            if (c == '\r') {
                continue;
            }

            // Handle LF (keep)
            if (c == '\n') {
                raw_buffer[raw_bytes_read++] = (uint8_t)c;
                chars_count++;
                continue;
            }

            // GB18030 character handling
            uint8_t first_byte = (uint8_t)c;
            raw_buffer[raw_bytes_read++] = first_byte;

            if (first_byte < 0x80) {
                // ASCII
                chars_count++;
            } else if (first_byte >= 0x81 && first_byte <= 0xFE) {
                // GB double-byte character - read second byte
                c = fgetc(reader->file);
                if (c == EOF) {
                    break;
                }

                uint8_t second_byte = (uint8_t)c;
                reader->position.file_position++;

                // Validate second byte
                if (second_byte >= 0x40 && second_byte <= 0xFE && second_byte != 0x7F) {
                    raw_buffer[raw_bytes_read++] = second_byte;
                    chars_count++;  // Count as one character
                } else {
                    // Invalid second byte, put it back
                    ungetc(c, reader->file);
                    reader->position.file_position--;
                }
            }
        }

        // Convert GB18030 buffer to UTF-8
        int utf8_len = gb18030_to_utf8(raw_buffer, raw_bytes_read, 
                                       text_buffer, buffer_size);
        if (utf8_len < 0) {
            ESP_LOGE(TAG, "GB18030 to UTF-8 conversion failed");
            text_buffer[0] = '\0';
            return -1;
        }

        ESP_LOGD(TAG, "Read page %d: %d GB chars -> %d UTF-8 bytes, file pos=%ld",
                 reader->position.page_number + 1, chars_count, utf8_len, 
                 reader->position.file_position);

    } else {
        // UTF-8 or ASCII mode: read UTF-8 characters directly
        size_t bytes_written = 0;
        chars_count = 0;

        while (chars_count < chars_per_page && bytes_written < buffer_size - 4) {
            int c = fgetc(reader->file);

            if (c == EOF) {
                break;
            }

            reader->position.file_position++;

            // Handle CR (skip)
            if (c == '\r') {
                continue;
            }

            // Handle LF (keep)
            if (c == '\n') {
                text_buffer[bytes_written++] = '\n';
                text_buffer[bytes_written] = '\0';
                chars_count++;
                continue;
            }

            // UTF-8 character handling
            if (c < 0x80) {
                // Single-byte ASCII
                text_buffer[bytes_written++] = (char)c;
                text_buffer[bytes_written] = '\0';
                chars_count++;
            } else {
                // Multi-byte UTF-8
                unsigned char first_byte = (unsigned char)c;
                int utf8_len = 0;

                // Determine UTF-8 character length
                if ((first_byte & 0xE0) == 0xC0) {
                    utf8_len = 2;
                } else if ((first_byte & 0xF0) == 0xE0) {
                    utf8_len = 3;
                } else if ((first_byte & 0xF8) == 0xF0) {
                    utf8_len = 4;
                } else {
                    // Invalid UTF-8 start byte
                    ESP_LOGD(TAG, "Invalid UTF-8 start byte: 0x%02X", first_byte);
                    continue;
                }

                // Check buffer space
                if (bytes_written + utf8_len >= buffer_size - 1) {
                    ungetc(c, reader->file);
                    reader->position.file_position--;
                    break;
                }

                // Write first byte
                text_buffer[bytes_written++] = (char)c;

                // Read continuation bytes
                bool valid_utf8 = true;
                for (int i = 1; i < utf8_len; i++) {
                    c = fgetc(reader->file);
                    if (c == EOF) {
                        valid_utf8 = false;
                        break;
                    }

                    unsigned char cont_byte = (unsigned char)c;
                    if ((cont_byte & 0xC0) != 0x80) {
                        // Invalid continuation byte
                        ungetc(c, reader->file);
                        valid_utf8 = false;
                        break;
                    }

                    reader->position.file_position++;
                    text_buffer[bytes_written++] = (char)c;
                }

                if (valid_utf8) {
                    text_buffer[bytes_written] = '\0';
                    chars_count++;
                }
            }
        }

        ESP_LOGD(TAG, "Read page %d: %d UTF-8 chars (%zu bytes), file pos=%ld",
                 reader->position.page_number + 1, chars_count, bytes_written, 
                 reader->position.file_position);
    }

    reader->position.page_number++;

    return chars_count;
}

bool txt_reader_goto_page(txt_reader_t *reader, int page_number, int chars_per_page) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    if (chars_per_page <= 0) {
        chars_per_page = 512;
    }

    // 简单实现：回到开头然后逐页读取
    if (page_number <= 0) {
        page_number = 1;
    }

    // 如果目标页在当前页之前，回到文件开头
    if (page_number < reader->position.page_number) {
        fseek(reader->file, 0, SEEK_SET);
        reader->position.page_number = 0;
        reader->position.file_position = 0;

        // 跳过 UTF-8 BOM
        if (reader->encoding == TXT_ENCODING_UTF8 && is_utf8_bom(reader->file)) {
            fseek(reader->file, 3, SEEK_SET);
        }
        reader->position.file_position = ftell(reader->file);
    }

    // 逐页读取直到目标页
    // 注意：该函数常在输入轮询任务里调用，栈很小，不能放大数组在栈上。
    // 分配一个足够容纳“单页 UTF-8 输出”的缓冲区即可。
    size_t tmp_size = (size_t)chars_per_page * 4u + 8u;
    if (tmp_size < 128u) {
        tmp_size = 128u;
    }
    if (tmp_size > 8192u) {
        tmp_size = 8192u;
    }

    char *temp_buffer = (char *)malloc(tmp_size);
    if (temp_buffer == NULL) {
        ESP_LOGE(TAG, "malloc failed in goto_page (tmp_size=%u)", (unsigned)tmp_size);
        return false;
    }

    while (reader->position.page_number < page_number - 1) {
        int n = txt_reader_read_page(reader, temp_buffer, tmp_size, chars_per_page);
        if (n <= 0) {
            free(temp_buffer);
            return false; // 已到文件末尾
        }
    }

    free(temp_buffer);

    ESP_LOGI(TAG, "Jumped to page %d (chars_per_page=%d)", page_number, chars_per_page);
    return true;
}

bool txt_reader_seek(txt_reader_t *reader, long position) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    if (position < 0) {
        position = 0;
    } else if (position > reader->position.file_size) {
        position = reader->position.file_size;
    }

    // UTF-8：避免 seek 到多字节字符的中间（continuation byte 0x80..0xBF）
    if (reader->encoding == TXT_ENCODING_UTF8 && reader->file != NULL && position > 0) {
        long adjusted = position;
        for (int i = 0; i < 4 && adjusted > 0; i++) {
            if (fseek(reader->file, adjusted, SEEK_SET) != 0) {
                break;
            }
            int c = fgetc(reader->file);
            if (c == EOF) {
                break;
            }
            if (((uint8_t)c & 0xC0u) == 0x80u) {
                // continuation byte: move back
                adjusted--;
                continue;
            }
            break;
        }
        position = adjusted;
    }

    int result = fseek(reader->file, position, SEEK_SET);
    if (result == 0) {
        reader->position.file_position = position;
        ESP_LOGI(TAG, "Seeked to position %ld", position);
        return true;
    }

    ESP_LOGE(TAG, "Seek failed to position %ld", position);
    return false;
}

txt_position_t txt_reader_get_position(const txt_reader_t *reader) {
    if (reader == NULL) {
        txt_position_t empty = {0};
        return empty;
    }
    return reader->position;
}

int txt_reader_get_total_pages(const txt_reader_t *reader, int chars_per_page) {
    if (reader == NULL || !reader->is_open) {
        return 0;
    }

    // Encoding-specific byte-to-character ratio estimates
    // Based on typical Chinese text files with mixed content
    #define GB18030_BYTES_PER_CHAR 16   // ~60% Chinese (2 bytes) + 40% ASCII (1 byte) = 1.6 bytes/char
    #define GB18030_CHARS_MULT     10   // Multiply by 10/16 = 0.625
    #define UTF8_BYTES_PER_CHAR    24   // ~70% Chinese (3 bytes) + 30% ASCII (1 byte) = 2.4 bytes/char
    #define UTF8_CHARS_MULT        10   // Multiply by 10/24 = 0.417

    // Estimate total pages based on encoding
    long file_size = reader->position.file_size;
    long estimated_chars;

    if (reader->encoding == TXT_ENCODING_GB18030) {
        // GB18030: Most Chinese characters are 2 bytes
        estimated_chars = file_size * GB18030_CHARS_MULT / GB18030_BYTES_PER_CHAR;
    } else if (reader->encoding == TXT_ENCODING_UTF8) {
        // UTF-8: Chinese characters are typically 3 bytes
        estimated_chars = file_size * UTF8_CHARS_MULT / UTF8_BYTES_PER_CHAR;
    } else {
        // ASCII: 1 byte per character
        estimated_chars = file_size;
    }

    // Calculate pages
    int pages = (int)((estimated_chars + chars_per_page - 1) / chars_per_page);

    // Ensure at least 1 page for non-empty files
    if (pages == 0 && file_size > 0) {
        pages = 1;
    }

    ESP_LOGD(TAG, "Estimated pages: %d (file_size=%ld, encoding=%d, chars_per_page=%d)",
             pages, file_size, reader->encoding, chars_per_page);

    return pages;
}

bool txt_reader_save_position(const txt_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return false;
    }

    const char *filename = strrchr(reader->file_path, '/');
    filename = (filename != NULL) ? (filename + 1) : reader->file_path;

    char key[16];
    make_nvs_key_for_txt_path(reader->file_path, key, sizeof(key));

    // 保存位置（byte offset）
    err = nvs_set_i32(nvs_handle, key, (int32_t)reader->position.file_position);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved position for %s: %ld (key=%s)", filename, reader->position.file_position, key);
        return true;
    }

    ESP_LOGE(TAG, "Failed to save position: %d", err);
    return false;
}

bool txt_reader_load_position(txt_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved position found (NVS open failed: %d)", err);
        return false;
    }

    const char *filename = strrchr(reader->file_path, '/');
    filename = (filename != NULL) ? (filename + 1) : reader->file_path;

    char key[16];
    make_nvs_key_for_txt_path(reader->file_path, key, sizeof(key));

    int32_t saved_pos = 0;
    err = nvs_get_i32(nvs_handle, key, &saved_pos);
    nvs_close(nvs_handle);

    if (err == ESP_OK && saved_pos >= 0) {
        (void)txt_reader_seek(reader, (long)saved_pos);
        ESP_LOGI(TAG, "Loaded position for %s: %ld (key=%s)", filename, (long)saved_pos, key);
        return true;
    }

    ESP_LOGW(TAG, "No saved position found for %s", filename);
    return false;
}

txt_encoding_t txt_reader_detect_encoding(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for encoding detection: %s", file_path);
        return TXT_ENCODING_UTF8;
    }

    // 检查 BOM
    if (is_utf8_bom(file)) {
        fclose(file);
        ESP_LOGI(TAG, "Detected UTF-8 with BOM: %s", file_path);
        return TXT_ENCODING_UTF8;
    }

    // 从内容检测
    txt_encoding_t encoding = detect_encoding_from_content(file);
    fclose(file);

    const char *encoding_str[] = {"UTF-8", "GB18030", "ASCII", "AUTO"};
    ESP_LOGI(TAG, "Detected encoding: %s for %s", encoding_str[encoding], file_path);

    return encoding;
}

void txt_reader_cleanup(txt_reader_t *reader) {
    if (reader == NULL) {
        return;
    }

    txt_reader_close(reader);

    if (reader->buffer != NULL) {
        free(reader->buffer);
        reader->buffer = NULL;
    }

    ESP_LOGI(TAG, "TXT reader cleaned up");
}

// NVS key for last read info
#define NVS_KEY_LAST_READ "last_read"

bool txt_reader_save_last_read(const char *file_path, int32_t file_position, int page_number) {
    if (file_path == NULL || file_path[0] == '\0') {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return false;
    }

    // Extract book name from path
    const char *filename = strrchr(file_path, '/');
    filename = (filename != NULL) ? (filename + 1) : file_path;

    // Save file path
    err = nvs_set_str(nvs_handle, NVS_KEY_LAST_READ "_path", file_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save file path: %d", err);
        nvs_close(nvs_handle);
        return false;
    }

    // Save file position
    err = nvs_set_i32(nvs_handle, NVS_KEY_LAST_READ "_pos", file_position);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save position: %d", err);
        nvs_close(nvs_handle);
        return false;
    }

    // Save page number
    err = nvs_set_i32(nvs_handle, NVS_KEY_LAST_READ "_page", page_number);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save page: %d", err);
        nvs_close(nvs_handle);
        return false;
    }

    // Commit
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved last read: %s (pos=%ld, page=%d)", filename, (long)file_position, page_number);
        return true;
    }

    ESP_LOGE(TAG, "Failed to commit: %d", err);
    return false;
}

last_read_info_t txt_reader_get_last_read(void) {
    last_read_info_t info = {0};
    info.valid = false;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No last read info (NVS open failed: %d)", err);
        return info;
    }

    // Get file path
    char file_path[256] = {0};
    size_t len = sizeof(file_path);
    err = nvs_get_str(nvs_handle, NVS_KEY_LAST_READ "_path", file_path, &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No last read path found");
        nvs_close(nvs_handle);
        return info;
    }

    // Get file position
    int32_t pos = 0;
    err = nvs_get_i32(nvs_handle, NVS_KEY_LAST_READ "_pos", &pos);
    if (err != ESP_OK) {
        pos = 0;
    }

    // Get page number
    int32_t page = 0;
    err = nvs_get_i32(nvs_handle, NVS_KEY_LAST_READ "_page", &page);
    if (err != ESP_OK) {
        page = 0;
    }

    nvs_close(nvs_handle);

    // Extract book name
    const char *filename = strrchr(file_path, '/');
    filename = (filename != NULL) ? (filename + 1) : file_path;

    strncpy(info.file_path, file_path, sizeof(info.file_path) - 1);
    info.file_path[sizeof(info.file_path) - 1] = '\0';
    strncpy(info.book_name, filename, sizeof(info.book_name) - 1);
    info.book_name[sizeof(info.book_name) - 1] = '\0';
    info.file_position = pos;
    info.page_number = page;
    info.valid = true;

    ESP_LOGI(TAG, "Got last read: %s (page=%d)", info.book_name, info.page_number);
    return info;
}

void txt_reader_clear_last_read(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    nvs_erase_key(nvs_handle, NVS_KEY_LAST_READ "_path");
    nvs_erase_key(nvs_handle, NVS_KEY_LAST_READ "_pos");
    nvs_erase_key(nvs_handle, NVS_KEY_LAST_READ "_page");
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Cleared last read info");
}
