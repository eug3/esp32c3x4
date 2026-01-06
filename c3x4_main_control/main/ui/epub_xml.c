/**
 * @file epub_xml.c
 * @brief EPUB XML 解析器实现 - 简化版，手动解析不依赖 TinyXML2
 */

#include "epub_xml.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "EPUB_XML";

struct epub_xml_parser {
    char *content;
    size_t length;
    size_t pos;
};

// XML 标签辅助函数
static const char* __attribute__((unused))
find_tag(const char *xml, size_t len, const char *tag, size_t *tag_len) {
    char open_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s", tag);

    const char *start = strstr(xml, open_tag);
    if (!start) return NULL;

    // 查找闭合的 '>'
    const char *end = strchr(start, '>');
    if (!end) return NULL;

    *tag_len = end - start + 1;
    return start;
}

static const char* __attribute__((unused))
find_closing_tag(const char *xml, size_t len, const char *tag) {
    char close_tag[64];
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
    return strstr(xml, close_tag);
}

// 提取标签属性值
static bool extract_attribute(const char *tag_start, const char *attr_name, char *value, size_t value_size) {
    const char *attr = strstr(tag_start, attr_name);
    if (!attr) return false;

    attr += strlen(attr_name);
    while (*attr == ' ' || *attr == '=') attr++;
    if (*attr == '"') attr++;

    const char *end = strchr(attr, '"');
    if (!end) return false;

    size_t len = end - attr;
    if (len >= value_size) len = value_size - 1;
    memcpy(value, attr, len);
    value[len] = '\0';

    return true;
}

epub_xml_parser_t* epub_xml_create(const char *xml_content, size_t content_length) {
    epub_xml_parser_t *parser = calloc(1, sizeof(epub_xml_parser_t));
    if (!parser) {
        ESP_LOGE(TAG, "Failed to allocate parser");
        return NULL;
    }

    parser->content = strndup(xml_content, content_length);
    parser->length = content_length;
    parser->pos = 0;

    return parser;
}

void epub_xml_destroy(epub_xml_parser_t *parser) {
    if (!parser) return;
    if (parser->content) {
        free(parser->content);
    }
    free(parser);
}

bool epub_xml_parse_metadata(epub_xml_parser_t *parser, epub_xml_metadata_t *metadata) {
    if (!parser || !metadata) return false;

    memset(metadata, 0, sizeof(epub_xml_metadata_t));

    // 查找 <metadata> 标签
    const char *metadata_start = strstr(parser->content, "<metadata");
    if (!metadata_start) {
        ESP_LOGE(TAG, "metadata tag not found");
        return false;
    }

    const char *metadata_end = strstr(metadata_start, "</metadata>");
    if (!metadata_end) {
        ESP_LOGE(TAG, "metadata end tag not found");
        return false;
    }

    // 提取 title
    const char *title_start = strstr(metadata_start, "<dc:title");
    if (title_start && title_start < metadata_end) {
        const char *value_start = strchr(title_start, '>');
        if (value_start) {
            value_start++;
            const char *value_end = strstr(value_start, "</dc:title>");
            if (value_end && value_end < metadata_end) {
                size_t len = value_end - value_start;
                if (len >= sizeof(metadata->title)) len = sizeof(metadata->title) - 1;
                memcpy(metadata->title, value_start, len);
                metadata->title[len] = '\0';
            }
        }
    }

    // 提取 author
    const char *author_start = strstr(metadata_start, "<dc:creator");
    if (author_start && author_start < metadata_end) {
        const char *value_start = strchr(author_start, '>');
        if (value_start) {
            value_start++;
            const char *value_end = strstr(value_start, "</dc:creator>");
            if (value_end && value_end < metadata_end) {
                size_t len = value_end - value_start;
                if (len >= sizeof(metadata->author)) len = sizeof(metadata->author) - 1;
                memcpy(metadata->author, value_start, len);
                metadata->author[len] = '\0';
            }
        }
    }

    ESP_LOGI(TAG, "Metadata: title='%s', author='%s'", metadata->title, metadata->author);
    return true;
}

int epub_xml_parse_spine(epub_xml_parser_t *parser,
                         epub_xml_spine_item_t *spine_items,
                         int max_items) {
    if (!parser || !spine_items || max_items <= 0) {
        return 0;
    }

    // 查找 <spine> 标签
    const char *spine_start = strstr(parser->content, "<spine");
    if (!spine_start) {
        ESP_LOGE(TAG, "spine tag not found");
        return 0;
    }

    const char *spine_end = strstr(spine_start, "</spine>");
    if (!spine_end) {
        ESP_LOGE(TAG, "spine end tag not found");
        return 0;
    }

    int count = 0;
    const char *itemref_start = spine_start;

    while (count < max_items) {
        itemref_start = strstr(itemref_start, "<itemref");
        if (!itemref_start || itemref_start >= spine_end) {
            break;
        }

        const char *itemref_end = strchr(itemref_start, '>');
        if (!itemref_end) break;

        // 提取 idref 属性
        epub_xml_spine_item_t *item = &spine_items[count];
        if (extract_attribute(itemref_start, "idref", item->idref, sizeof(item->idref))) {
            item->index = count;
            // href 需要从 manifest 查找，这里先留空
            item->href[0] = '\0';
            count++;
        }

        itemref_start = itemref_end + 1;
    }

    ESP_LOGI(TAG, "Parsed %d spine items", count);
    return count;
}

bool epub_xml_find_manifest_item(epub_xml_parser_t *parser,
                                  const char *idref,
                                  char *href,
                                  size_t href_size) {
    if (!parser || !idref || !href) {
        return false;
    }

    // 查找 <manifest> 标签
    const char *manifest_start = strstr(parser->content, "<manifest");
    if (!manifest_start) {
        ESP_LOGE(TAG, "manifest tag not found");
        return false;
    }

    const char *manifest_end = strstr(manifest_start, "</manifest>");
    if (!manifest_end) {
        return false;
    }

    // 查找匹配的 <item> 标签
    char search_tag[128];
    snprintf(search_tag, sizeof(search_tag), "id=\"%s\"", idref);

    const char *item_start = strstr(manifest_start, search_tag);
    if (!item_start || item_start >= manifest_end) {
        ESP_LOGW(TAG, "manifest item with id='%s' not found", idref);
        return false;
    }

    // 提取 href 属性
    return extract_attribute(item_start, "href", href, href_size);
}

bool epub_xml_find_content_opf(epub_xml_parser_t *parser,
                                char *content_opf_path,
                                size_t path_size) {
    if (!parser || !content_opf_path) {
        return false;
    }

    // EPUB 标准路径: META-INF/container.xml 定义了 .opf 文件位置
    // 这里我们假设标准路径，实际应用中需要先解析 container.xml

    // 尝试常见路径
    const char *common_paths[] = {
        "OEBPS/content.opf",
        "OPS/content.opf",
        "content.opf",
        NULL
    };

    for (int i = 0; common_paths[i]; i++) {
        if (strstr(parser->content, common_paths[i])) {
            strncpy(content_opf_path, common_paths[i], path_size - 1);
            content_opf_path[path_size - 1] = '\0';
            ESP_LOGI(TAG, "Found content.opf at: %s", content_opf_path);
            return true;
        }
    }

    // 默认返回 OEBPS/content.opf
    strncpy(content_opf_path, "OEBPS/content.opf", path_size - 1);
    content_opf_path[path_size - 1] = '\0';
    return false;
}
