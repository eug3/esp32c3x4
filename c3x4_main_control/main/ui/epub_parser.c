/**
 * @file epub_parser.c
 * @brief EPUB 电子书解析器实现 - 流式解析版本
 *
 * 使用流式解析器按需读取 EPUB 内容，节省内存
 * 适配 ESP32-C3 (400KB RAM)
 */

#include "epub_parser.h"
#include "epub_zip.h"
#include "epub_cache.h"
#include "epub_xml.h"
#include "epub_html.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

static const char *TAG = "EPUB_PARSER";

// NVS 命名空间
#define NVS_NAMESPACE "reader_pos"
// 注意：ESP-IDF NVS key 最长 15 字符，且建议只用 [0-9A-Za-z_]
#define NVS_KEY_PREFIX "ep_"

static uint32_t fnv1a32_str(const char *s)
{
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

static uint32_t make_epub_hash(const char *epub_path)
{
    return fnv1a32_str(epub_path);
}

// 最大章节数
#define MAX_CHAPTERS 200

// EPUB MIME 类型 (保留供将来使用)
static const char *EPUB_MIME_TYPES[] __attribute__((unused)) = {
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

    // LittleFS 缓存（用于加速重复打开/翻章，减少 SD 访问）
    (void)epub_cache_init();

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

    // 记录 opf 所在目录（章节 href 通常是相对 opf 的路径）
    char opf_dir[128] = {0};
    const char *slash = strrchr(opf_file.filename, '/');
    if (slash != NULL) {
        size_t dir_len = (size_t)(slash - opf_file.filename + 1);
        if (dir_len >= sizeof(opf_dir)) {
            dir_len = sizeof(opf_dir) - 1;
        }
        memcpy(opf_dir, opf_file.filename, dir_len);
        opf_dir[dir_len] = '\0';
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
    // 注意：spine_items 结构体较大（含 href[256]），不能放在小栈任务上。
    epub_xml_spine_item_t *spine_items = calloc(MAX_CHAPTERS, sizeof(epub_xml_spine_item_t));
    if (!spine_items) {
        ESP_LOGE(TAG, "Failed to allocate spine items");
        epub_xml_destroy(xml);
        free(opf_buffer);
        epub_zip_close(zip);
        return false;
    }

    int spine_count = epub_xml_parse_spine(xml, spine_items, MAX_CHAPTERS);
    ESP_LOGI(TAG, "Found %d spine items", spine_count);

    // 分配章节数组
    reader->chapters = calloc(spine_count, sizeof(epub_chapter_t));
    if (!reader->chapters) {
        ESP_LOGE(TAG, "Failed to allocate chapters array");
        free(spine_items);
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
            // 组合 opf_dir + href（href 为相对路径）
            // 注意：避免 snprintf 触发 format-truncation 警告（项目里当作 error）。
            char full_path[256];
            full_path[0] = '\0';
            if (opf_dir[0] != '\0' && href[0] != '/' && strstr(href, "://") == NULL) {
                size_t dlen = strlen(opf_dir);
                size_t hlen = strlen(href);
                if (dlen >= sizeof(full_path)) {
                    dlen = sizeof(full_path) - 1;
                }
                memcpy(full_path, opf_dir, dlen);
                size_t remain = sizeof(full_path) - 1 - dlen;
                if (hlen > remain) {
                    hlen = remain;
                }
                memcpy(full_path + dlen, href, hlen);
                full_path[dlen + hlen] = '\0';
            } else {
                size_t hlen = strlen(href);
                if (hlen >= sizeof(full_path)) {
                    hlen = sizeof(full_path) - 1;
                }
                memcpy(full_path, href, hlen);
                full_path[hlen] = '\0';
            }

            size_t fp_len = strlen(full_path);
            size_t max_copy = sizeof(reader->chapters[valid_chapters].content_file) - 1;
            if (fp_len > max_copy) {
                fp_len = max_copy;
            }
            memcpy(reader->chapters[valid_chapters].content_file, full_path, fp_len);
            reader->chapters[valid_chapters].content_file[fp_len] = '\0';
            reader->chapters[valid_chapters].chapter_index = valid_chapters;
            snprintf(reader->chapters[valid_chapters].title,
                    sizeof(reader->chapters[valid_chapters].title),
                    "Chapter %d", valid_chapters + 1);
            valid_chapters++;
        }
    }

    reader->metadata.total_chapters = valid_chapters;

    // 清理
    free(spine_items);
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

    if (buffer_size <= 1) {
        return -1;
    }

    // 先尝试从 LittleFS 缓存读取
    epub_cache_key_t cache_key;
    memset(&cache_key, 0, sizeof(cache_key));
    cache_key.type = EPUB_CACHE_CHAPTER;
    strncpy(cache_key.epub_path, reader->epub_path, sizeof(cache_key.epub_path) - 1);
    strncpy(cache_key.content_path, chapter->content_file, sizeof(cache_key.content_path) - 1);

    if (epub_cache_exists(&cache_key)) {
        int n = epub_cache_read(&cache_key, text_buffer, buffer_size - 1);
        if (n > 0) {
            text_buffer[n] = '\0';
            return n;
        }
    }

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
        ESP_LOGE(TAG, "Chapter file not found: %s", chapter->content_file);
        epub_zip_close(zip);
        return -1;
    }

    // 使用 ZIP 内的真实路径作为缓存键（更稳定）
    strncpy(cache_key.content_path, chapter_file.filename, sizeof(cache_key.content_path) - 1);
    cache_key.content_path[sizeof(cache_key.content_path) - 1] = '\0';

    if (!epub_cache_exists(&cache_key)) {
        char cache_path[256];
        if (epub_cache_get_file_path(&cache_key, cache_path, sizeof(cache_path))) {
            int ext_ret = epub_zip_extract_file_to_path(zip, &chapter_file, cache_path);
            if (ext_ret < 0) {
                // 写缓存失败不影响阅读：继续走内存解压
                ESP_LOGW(TAG, "Failed to precache chapter to %s", cache_path);
            }
        }
    }

    // 再次尝试从缓存读（即使 buffer 较小，也可读前 N 字节用于显示）
    if (epub_cache_exists(&cache_key)) {
        int n = epub_cache_read(&cache_key, text_buffer, buffer_size - 1);
        if (n > 0) {
            text_buffer[n] = '\0';
            epub_zip_close(zip);
            return n;
        }
    }

    // 解压章节内容
    int bytes_read = epub_zip_extract_file(zip, &chapter_file, text_buffer, buffer_size - 1);
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

    // 生成短 key：ep_XXXXXXXX_ch / ep_XXXXXXXX_pg （总长度 14）
    uint32_t h = make_epub_hash(reader->epub_path);
    char key[16];

    // 保存章节
    (void)snprintf(key, sizeof(key), "ep_%08" PRIx32 "_ch", h);
    err = nvs_set_i32(nvs_handle, key, (int32_t)reader->position.current_chapter);

    if (err == ESP_OK) {
        (void)snprintf(key, sizeof(key), "ep_%08" PRIx32 "_pg", h);
        err = nvs_set_i32(nvs_handle, key, (int32_t)reader->position.page_number);
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

    uint32_t h = make_epub_hash(reader->epub_path);
    char key[16];
    (void)snprintf(key, sizeof(key), "ep_%08" PRIx32 "_ch", h);

    int32_t saved_chapter = 0;
    err = nvs_get_i32(nvs_handle, key, &saved_chapter);

    if (err == ESP_OK) {
        (void)snprintf(key, sizeof(key), "ep_%08" PRIx32 "_pg", h);
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
