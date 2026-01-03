/**
 * @file epub_parser.c
 * @brief EPUB 电子书解析器实现 - 流式解析版本
 *
 * 使用流式解析器按需读取 EPUB 内容，节省内存
 * 适配 ESP32-C3 (400KB RAM)
 */

#include "epub_parser.h"
#include "epub_zip.h"
#include "epub_xml.h"
#include "epub_html.h"
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

    ESP_LOGI(TAG, "Opening EPUB: %s", epub_path);

    // 步骤 1: 打开 ZIP 文件
    epub_zip_t *zip = epub_zip_open(epub_path);
    if (!zip) {
        ESP_LOGE(TAG, "Failed to open EPUB as ZIP");
        return false;
    }

    // 步骤 2: 查找并解析 content.opf
    // 常见路径
    const char *opf_paths[] = {
        "OEBPS/content.opf",
        "OPS/content.opf",
        "content.opf",
        NULL
    };

    epub_zip_file_info_t opf_file;
    bool found_opf = false;

    for (int i = 0; opf_paths[i]; i++) {
        if (epub_zip_find_file(zip, opf_paths[i], &opf_file)) {
            ESP_LOGI(TAG, "Found content.opf at: %s", opf_paths[i]);
            found_opf = true;
            break;
        }
    }

    if (!found_opf) {
        ESP_LOGE(TAG, "content.opf not found in EPUB");
        epub_zip_close(zip);
        return false;
    }

    // 步骤 3: 读取并解析 content.opf
    char *opf_buffer = malloc(opf_file.uncompressed_size + 1);
    if (!opf_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for content.opf");
        epub_zip_close(zip);
        return false;
    }

    int opf_size = epub_zip_extract_file(zip, &opf_file, opf_buffer, opf_file.uncompressed_size);
    if (opf_size < 0) {
        ESP_LOGE(TAG, "Failed to extract content.opf");
        free(opf_buffer);
        epub_zip_close(zip);
        return false;
    }
    opf_buffer[opf_size] = '\0';

    // 创建 XML 解析器
    epub_xml_parser_t *xml = epub_xml_create(opf_buffer, opf_size);
    if (!xml) {
        ESP_LOGE(TAG, "Failed to create XML parser");
        free(opf_buffer);
        epub_zip_close(zip);
        return false;
    }

    // 解析元数据
    epub_xml_metadata_t xml_metadata;
    if (epub_xml_parse_metadata(xml, &xml_metadata)) {
        strncpy(reader->metadata.title, xml_metadata.title, sizeof(reader->metadata.title) - 1);
        strncpy(reader->metadata.author, xml_metadata.author, sizeof(reader->metadata.author) - 1);
        strncpy(reader->metadata.language, xml_metadata.language, sizeof(reader->metadata.language) - 1);
        ESP_LOGI(TAG, "Metadata: title='%s', author='%s'", reader->metadata.title, reader->metadata.author);
    } else {
        // 使用文件名作为标题
        const char *filename = strrchr(epub_path, '/');
        strncpy(reader->metadata.title, filename ? filename + 1 : epub_path, sizeof(reader->metadata.title) - 1);
        char *dot = strrchr(reader->metadata.title, '.');
        if (dot) *dot = '\0';
        strncpy(reader->metadata.author, "Unknown", sizeof(reader->metadata.author) - 1);
    }

    // 解析 spine (章节顺序)
    epub_xml_spine_item_t spine_items[MAX_CHAPTERS];
    int spine_count = epub_xml_parse_spine(xml, spine_items, MAX_CHAPTERS);
    ESP_LOGI(TAG, "Found %d spine items", spine_count);

    // 分配章节数组
    reader->chapters = calloc(spine_count, sizeof(epub_chapter_t));
    if (!reader->chapters) {
        ESP_LOGE(TAG, "Failed to allocate chapters array");
        epub_xml_destroy(xml);
        free(opf_buffer);
        epub_zip_close(zip);
        return false;
    }

    // 解析 manifest 并构建完整章节列表
    int valid_chapters = 0;
    for (int i = 0; i < spine_count; i++) {
        char href[256];
        if (epub_xml_find_manifest_item(xml, spine_items[i].idref, href, sizeof(href))) {
            // 构建完整路径 - 确保 href 不会超出目标缓冲区
            size_t href_len = strlen(href);
            size_t max_copy = sizeof(reader->chapters[valid_chapters].content_file) - 1;
            if (href_len > max_copy) {
                href_len = max_copy;
            }
            strncpy(reader->chapters[valid_chapters].content_file, href, href_len);
            reader->chapters[valid_chapters].content_file[href_len] = '\0';
            reader->chapters[valid_chapters].chapter_index = valid_chapters;
            snprintf(reader->chapters[valid_chapters].title,
                    sizeof(reader->chapters[valid_chapters].title),
                    "Chapter %d", valid_chapters + 1);
            valid_chapters++;
        }
    }

    reader->metadata.total_chapters = valid_chapters;

    // 清理
    epub_xml_destroy(xml);
    free(opf_buffer);
    epub_zip_close(zip);

    reader->is_open = true;
    reader->is_unzipped = false;  // 流式解析，不需要预解压
    reader->position.current_chapter = 0;
    reader->position.page_number = 0;

    ESP_LOGI(TAG, "Opened EPUB: %s (%d chapters)",
             reader->metadata.title, reader->metadata.total_chapters);

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

    // 从 EPUB 中流式读取章节内容
    // 重新打开 ZIP（因为之前已关闭）
    epub_zip_t *zip = epub_zip_open(reader->epub_path);
    if (!zip) {
        ESP_LOGE(TAG, "Failed to reopen EPUB");
        return -1;
    }

    // 查找章节文件
    epub_zip_file_info_t chapter_file;
    if (!epub_zip_find_file(zip, chapter->content_file, &chapter_file)) {
        // 尝试添加 OEBPS/ 前缀
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "OEBPS/%s", chapter->content_file);
        if (!epub_zip_find_file(zip, full_path, &chapter_file)) {
            ESP_LOGE(TAG, "Chapter file not found: %s", chapter->content_file);
            epub_zip_close(zip);
            return -1;
        }
    }

    // 解压章节内容
    int bytes_read = epub_zip_extract_file(zip, &chapter_file, text_buffer, buffer_size);
    if (bytes_read < 0) {
        ESP_LOGE(TAG, "Failed to extract chapter: %s", chapter->content_file);
        epub_zip_close(zip);
        return -1;
    }

    text_buffer[bytes_read] = '\0';
    epub_zip_close(zip);

    // 如果是 HTML，需要提取纯文本
    // 简化版：直接返回 HTML 内容，由上层解析
    ESP_LOGD(TAG, "Read chapter %d: %d bytes", chapter_index, bytes_read);

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
