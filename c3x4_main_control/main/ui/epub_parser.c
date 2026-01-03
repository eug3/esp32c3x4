/**
 * @file epub_parser.c
 * @brief EPUB 电子书解析器实现
 *
 * 注意：这是一个简化的 EPUB 解析器，支持基本的 EPUB 2.0/3.0 结构
 * 完整实现需要支持 ZIP 解压、XML 解析等复杂功能
 * 此版本为轻量级实现，适用于资源受限的 ESP32-C3
 */

#include "epub_parser.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "EPUB_PARSER";

// NVS 命名空间
#define NVS_NAMESPACE "reader_pos"
#define NVS_KEY_PREFIX "epub_"

// 最大章节数
#define MAX_CHAPTERS 200

// EPUB MIME 类型
static const char *EPUB_MIME_TYPES[] = {
    "application/epub+zip",
    "application/oebps-package+xml",
    "application/xhtml+xml",
    NULL
};

bool epub_parser_init(epub_reader_t *reader) {
    if (reader == NULL) {
        ESP_LOGE(TAG, "Invalid reader pointer");
        return false;
    }

    memset(reader, 0, sizeof(epub_reader_t));
    reader->current_file = NULL;

    ESP_LOGI(TAG, "EPUB parser initialized");
    return true;
}

bool epub_parser_is_valid_epub(const char *file_path) {
    if (file_path == NULL) {
        return false;
    }

    // 检查文件扩展名
    const char *ext = strrchr(file_path, '.');
    if (ext == NULL) {
        return false;
    }

    if (strcasecmp(ext, ".epub") != 0) {
        return false;
    }

    // 检查文件是否可以打开
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        return false;
    }

    // 检查 ZIP 文件头（EPUB 是 ZIP 格式）
    unsigned char zip_header[4];
    bool is_zip = (fread(zip_header, 1, 4, file) == 4 &&
                   zip_header[0] == 0x50 && zip_header[1] == 0x4B &&
                   (zip_header[2] == 0x03 || zip_header[2] == 0x05 ||
                    zip_header[2] == 0x07 || zip_header[2] == 0x08));

    fclose(file);

    if (!is_zip) {
        ESP_LOGW(TAG, "File is not a valid ZIP/EPUB: %s", file_path);
    }

    return is_zip;
}

bool epub_parser_open(epub_reader_t *reader, const char *epub_path) {
    if (reader == NULL || epub_path == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    if (!epub_parser_is_valid_epub(epub_path)) {
        ESP_LOGE(TAG, "Invalid EPUB file: %s", epub_path);
        return false;
    }

    strncpy(reader->epub_path, epub_path, sizeof(reader->epub_path) - 1);
    reader->epub_path[sizeof(reader->epub_path) - 1] = '\0';

    // 检查是否有预解压的目录
    // ESP32-C3 内存有限，需要在主机上预解压 EPUB 到 SD 卡
    // 预解压目录格式：/sdcard/XTCache/[书名]/
    const char *filename = strrchr(epub_path, '/');
    if (filename != NULL) {
        filename++;
    } else {
        filename = epub_path;
    }

    // 构建解压路径
    snprintf(reader->extract_path, sizeof(reader->extract_path),
             "/sdcard/XTCache/%.200s", filename);
    size_t len = strlen(reader->extract_path);
    if (len > 5 && strcmp(reader->extract_path + len - 5, ".epub") == 0) {
        reader->extract_path[len - 5] = '\0';
    }

    // 尝试打开解压目录
    // 这里假设 EPUB 已经在主机上解压到 /sdcard/XTCache/[书名]/ 目录
    // 目录结构应该包含：
    // - /sdcard/XTCache/[书名]/OEBPS/ 或 /sdcard/XTCache/[书名]/OPS/
    // - /sdcard/XTCache/[书名]/META-INF/container.xml
    // - /sdcard/XTCache/[书名]/content.opf

    ESP_LOGI(TAG, "EPUB file: %s", epub_path);
    ESP_LOGI(TAG, "Extract path (pre-extracted on host): %s", reader->extract_path);

    // 简化实现：假设预解压的文本文件存在
    // 实际项目中应该解析 container.xml 和 .opf 文件获取章节信息

    // 读取元数据（简化版本）
    strncpy(reader->metadata.title, filename, sizeof(reader->metadata.title) - 1);
    char *dot = strrchr(reader->metadata.title, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    strncpy(reader->metadata.author, "Unknown", sizeof(reader->metadata.author) - 1);
    strncpy(reader->metadata.language, "zh", sizeof(reader->metadata.language) - 1);

    // 分配章节数组
    reader->chapters = malloc(sizeof(epub_chapter_t) * MAX_CHAPTERS);
    if (reader->chapters == NULL) {
        ESP_LOGE(TAG, "Failed to allocate chapters array");
        return false;
    }
    memset(reader->chapters, 0, sizeof(epub_chapter_t) * MAX_CHAPTERS);

    // 简化版本：扫描目录获取章节列表
    // 假设章节文件命名为 chapter_001.txt, chapter_002.txt 等
    // 或者直接使用单个 .txt 文件

    reader->is_open = true;
    reader->position.current_chapter = 0;
    reader->position.page_number = 0;

    ESP_LOGI(TAG, "Opened EPUB (simplified mode): %s", reader->metadata.title);

    return true;
}

void epub_parser_close(epub_reader_t *reader) {
    if (reader == NULL) {
        return;
    }

    if (reader->current_file != NULL) {
        fclose(reader->current_file);
        reader->current_file = NULL;
    }

    if (reader->chapters != NULL) {
        free(reader->chapters);
        reader->chapters = NULL;
    }

    reader->is_open = false;
    ESP_LOGI(TAG, "EPUB parser closed");
}

const epub_metadata_t* epub_parser_get_metadata(const epub_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return NULL;
    }
    return &reader->metadata;
}

int epub_parser_get_chapter_count(const epub_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return 0;
    }
    return reader->metadata.total_chapters;
}

const epub_chapter_t* epub_parser_get_chapter(const epub_reader_t *reader, int chapter_index) {
    if (reader == NULL || !reader->is_open) {
        return NULL;
    }

    if (chapter_index < 0 || chapter_index >= reader->metadata.total_chapters) {
        return NULL;
    }

    return &reader->chapters[chapter_index];
}

int epub_parser_read_chapter(const epub_reader_t *reader, int chapter_index,
                             char *text_buffer, size_t buffer_size) {
    if (reader == NULL || !reader->is_open || text_buffer == NULL) {
        ESP_LOGE(TAG, "Invalid reader or buffer");
        return -1;
    }

    if (chapter_index < 0 || chapter_index >= reader->metadata.total_chapters) {
        ESP_LOGE(TAG, "Invalid chapter index: %d", chapter_index);
        return -1;
    }

    const epub_chapter_t *chapter = &reader->chapters[chapter_index];

    // 打开章节文件
    FILE *file = fopen(chapter->content_file, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open chapter file: %s", chapter->content_file);
        return -1;
    }

    // 读取文件内容
    size_t bytes_read = fread(text_buffer, 1, buffer_size - 1, file);
    text_buffer[bytes_read] = '\0';

    fclose(file);

    ESP_LOGD(TAG, "Read chapter %d: %zu bytes", chapter_index, bytes_read);

    return bytes_read;
}

bool epub_parser_goto_chapter(epub_reader_t *reader, int chapter_index) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    if (chapter_index < 0 || chapter_index >= reader->metadata.total_chapters) {
        ESP_LOGE(TAG, "Invalid chapter index: %d", chapter_index);
        return false;
    }

    // 关闭当前文件
    if (reader->current_file != NULL) {
        fclose(reader->current_file);
        reader->current_file = NULL;
    }

    reader->position.current_chapter = chapter_index;
    reader->position.chapter_position = 0;

    ESP_LOGI(TAG, "Jumped to chapter %d: %s",
             chapter_index, reader->chapters[chapter_index].title);

    return true;
}

bool epub_parser_next_chapter(epub_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    int next_chapter = reader->position.current_chapter + 1;
    if (next_chapter >= reader->metadata.total_chapters) {
        ESP_LOGW(TAG, "Already at last chapter");
        return false;
    }

    return epub_parser_goto_chapter(reader, next_chapter);
}

bool epub_parser_prev_chapter(epub_reader_t *reader) {
    if (reader == NULL || !reader->is_open) {
        return false;
    }

    int prev_chapter = reader->position.current_chapter - 1;
    if (prev_chapter < 0) {
        ESP_LOGW(TAG, "Already at first chapter");
        return false;
    }

    return epub_parser_goto_chapter(reader, prev_chapter);
}

epub_position_t epub_parser_get_position(const epub_reader_t *reader) {
    if (reader == NULL) {
        epub_position_t empty = {0};
        return empty;
    }
    return reader->position;
}

bool epub_parser_save_position(const epub_reader_t *reader) {
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
    const char *filename = strrchr(reader->epub_path, '/');
    if (filename == NULL) {
        filename = reader->epub_path;
    } else {
        filename++;  // 跳过 '/'
    }

    char key[64];
    char base_key[32];  // 存储基础键名

    // 创建基础键名（截断文件名到 20 字符）
    snprintf(base_key, sizeof(base_key), "%.20s", filename);
    // 替换文件名中的特殊字符为下划线
    for (char *p = base_key; *p; p++) {
        if (*p == ' ' || *p == '.' || *p == '-' || *p == '/') {
            *p = '_';
        }
    }

    // 保存章节
    snprintf(key, sizeof(key), "%s%s_ch", NVS_KEY_PREFIX, base_key);
    err = nvs_set_i32(nvs_handle, key, reader->position.current_chapter);

    if (err == ESP_OK) {
        snprintf(key, sizeof(key), "%s%s_pg", NVS_KEY_PREFIX, base_key);
        err = nvs_set_i32(nvs_handle, key, reader->position.page_number);
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved position for %s: chapter=%d, page=%d",
                 filename, reader->position.current_chapter,
                 reader->position.page_number);
        return true;
    }

    ESP_LOGE(TAG, "Failed to save position: %d", err);
    return false;
}

bool epub_parser_load_position(epub_reader_t *reader) {
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
    const char *filename = strrchr(reader->epub_path, '/');
    if (filename == NULL) {
        filename = reader->epub_path;
    } else {
        filename++;  // 跳过 '/'
    }

    char key[64];
    char base_key[32];  // 存储基础键名

    // 创建基础键名（截断文件名到 20 字符）
    snprintf(base_key, sizeof(base_key), "%.20s", filename);
    // 替换文件名中的特殊字符为下划线
    for (char *p = base_key; *p; p++) {
        if (*p == ' ' || *p == '.' || *p == '-' || *p == '/') {
            *p = '_';
        }
    }

    snprintf(key, sizeof(key), "%s%s_ch", NVS_KEY_PREFIX, base_key);

    int32_t saved_chapter = 0;
    err = nvs_get_i32(nvs_handle, key, &saved_chapter);

    if (err == ESP_OK) {
        snprintf(key, sizeof(key), "%s%s_pg", NVS_KEY_PREFIX, base_key);
        int32_t saved_page = 0;
        err = nvs_get_i32(nvs_handle, key, &saved_page);

        if (err == ESP_OK && saved_chapter >= 0 && saved_chapter < reader->metadata.total_chapters) {
            epub_parser_goto_chapter((epub_reader_t *)reader, saved_chapter);
            reader->position.page_number = saved_page;
            ESP_LOGI(TAG, "Loaded position for %s: chapter=%ld, page=%ld",
                     filename, saved_chapter, saved_page);
            nvs_close(nvs_handle);
            return true;
        }
    }

    nvs_close(nvs_handle);
    ESP_LOGW(TAG, "No saved position found for %s", filename);
    return false;
}

void epub_parser_cleanup(epub_reader_t *reader) {
    if (reader == NULL) {
        return;
    }

    epub_parser_close(reader);

    if (reader->chapters != NULL) {
        free(reader->chapters);
        reader->chapters = NULL;
    }

    ESP_LOGI(TAG, "EPUB parser cleaned up");
}
