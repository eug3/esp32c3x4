/**
 * @file txt_reader.c
 * @brief TXT 文本文件阅读器实现
 */

#include "txt_reader.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "TXT_READER";

// NVS 命名空间
#define NVS_NAMESPACE "reader_pos"
#define NVS_KEY_PREFIX "txt_"

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
    bool has_gb_pattern = false;

    for (size_t i = 0; i < n; i++) {
        if (buffer[i] > 0x7F) {
            is_ascii = false;

            // GB18030/GBK 特征：第一个字节在 0x81-0xFE 之间
            // 且第二个字节在 0x40-0xFE 之间
            if (buffer[i] >= 0x81 && buffer[i] <= 0xFE && i + 1 < n) {
                unsigned char next = buffer[i + 1];
                if (next >= 0x40 && next <= 0xFE) {
                    has_gb_pattern = true;
                }
            }
        }
    }

    if (is_ascii) {
        return TXT_ENCODING_ASCII;
    }

    // 如果有 GB 模式，优先认为是 GB18030
    if (has_gb_pattern) {
        return TXT_ENCODING_GB18030;
    }

    // 默认假设为 UTF-8
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

    reader->file = fopen(file_path, "r");
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
    reader->position.file_position = 0;
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

    size_t bytes_read = 0;
    int chars_read = 0;
    bool in_newline = false;

    text_buffer[0] = '\0';

    while (chars_read < chars_per_page && bytes_read < buffer_size - 1) {
        int c = fgetc(reader->file);

        if (c == EOF) {
            break;
        }

        reader->position.file_position++;

        // 处理换行符
        if (c == '\r') {
            continue; // 跳过 CR
        }

        if (c == '\n') {
            text_buffer[bytes_read++] = '\n';
            text_buffer[bytes_read] = '\0';
            chars_read++;
            in_newline = true;
            continue;
        }

        // 如果换行后第一个字符是空白符，可能是段落缩进
        if (in_newline && isspace(c)) {
            // 保留一个空格作为缩进
            text_buffer[bytes_read++] = ' ';
            bytes_read = (bytes_read < buffer_size) ? bytes_read : buffer_size - 1;
            text_buffer[bytes_read] = '\0';
            chars_read++;
            in_newline = false;

            // 跳过后续空白符
            while (isspace(c = fgetc(reader->file)) && c != '\n' && c != '\r') {
                reader->position.file_position++;
            }
            ungetc(c, reader->file);
            reader->position.file_position--;
            continue;
        }

        in_newline = false;
        text_buffer[bytes_read++] = (char)c;
        text_buffer[bytes_read] = '\0';
        chars_read++;
    }

    reader->position.page_number++;

    ESP_LOGD(TAG, "Read page %d: %d chars, pos=%ld",
             reader->position.page_number, chars_read, reader->position.file_position);

    return chars_read;
}

bool txt_reader_goto_page(txt_reader_t *reader, int page_number) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    // 简单实现：回到开头然后逐页读取
    if (page_number <= 0) {
        page_number = 1;
    }

    // 如果目标页在当前页之前，回到文件开头
    if (page_number < reader->position.page_number) {
        fseek(reader->file, 0, SEEK_SET);
        reader->position.page_number = 0;

        // 跳过 UTF-8 BOM
        if (reader->encoding == TXT_ENCODING_UTF8 && is_utf8_bom(reader->file)) {
            fseek(reader->file, 3, SEEK_SET);
        }
    }

    // 逐页读取直到目标页
    char temp_buffer[512];
    while (reader->position.page_number < page_number - 1) {
        int n = txt_reader_read_page(reader, temp_buffer, sizeof(temp_buffer), 512);
        if (n <= 0) {
            return false; // 已到文件末尾
        }
    }

    ESP_LOGI(TAG, "Jumped to page %d", page_number);
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

    // 估算总页数
    long estimated_chars = reader->position.file_size;
    int pages = (int)((estimated_chars + chars_per_page - 1) / chars_per_page);

    // 考虑到换行和空白，适当增加一些余量
    pages = (pages * 12) / 10;

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

    // 使用文件名（不含路径）作为 key，避免键过长
    const char *filename = strrchr(reader->file_path, '/');
    if (filename == NULL) {
        filename = reader->file_path;
    } else {
        filename++;  // 跳过 '/'
    }

    char key[64];
    int key_len = snprintf(key, sizeof(key), "%s%.50s", NVS_KEY_PREFIX, filename);
    if (key_len >= (int)sizeof(key)) {
        key[sizeof(key) - 1] = '\0';
    }

    // 保存位置
    err = nvs_set_i32(nvs_handle, key, reader->position.file_position);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved position for %s: %ld", filename, reader->position.file_position);
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

    // 使用文件名（不含路径）作为 key，与 save_position 保持一致
    const char *filename = strrchr(reader->file_path, '/');
    if (filename == NULL) {
        filename = reader->file_path;
    } else {
        filename++;  // 跳过 '/'
    }

    char key[64];
    int key_len = snprintf(key, sizeof(key), "%s%.50s", NVS_KEY_PREFIX, filename);
    if (key_len >= (int)sizeof(key)) {
        key[sizeof(key) - 1] = '\0';
    }

    int32_t saved_pos = 0;
    err = nvs_get_i32(nvs_handle, key, &saved_pos);
    nvs_close(nvs_handle);

    if (err == ESP_OK && saved_pos > 0) {
        txt_reader_seek(reader, saved_pos);
        ESP_LOGI(TAG, "Loaded position for %s: %ld", filename, saved_pos);
        return true;
    }

    ESP_LOGW(TAG, "No saved position found for %s", filename);
    return false;
}

txt_encoding_t txt_reader_detect_encoding(const char *file_path) {
    FILE *file = fopen(file_path, "r");
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
