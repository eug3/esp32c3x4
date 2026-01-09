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
    if (!tag_start || !attr_name || !value || value_size == 0) {
        return false;
    }
    
    // 查找属性名
    const char *search = tag_start;
    const char *attr = NULL;
    
    // 多次尝试不同的属性格式，以处理可能的空白和引号变化
    while ((attr = strstr(search, attr_name)) != NULL) {
        // 检查属性名之前是否为空白或开始
        if (attr != tag_start) {
            char before = *(attr - 1);
            if (!isspace((unsigned char)before) && before != '\0') {
                search = attr + strlen(attr_name);
                continue;
            }
        }
        
        // 检查属性名之后是否为'='
        const char *after_name = attr + strlen(attr_name);
        while (isspace((unsigned char)*after_name)) after_name++;
        if (*after_name != '=') {
            search = attr + strlen(attr_name);
            continue;
        }
        
        // 找到了正确的属性
        attr = after_name + 1;
        while (isspace((unsigned char)*attr)) attr++;
        
        // 处理引号（双引号或单引号）
        char quote = '\0';
        if (*attr == '"' || *attr == '\'') {
            quote = *attr;
            attr++;
        } else {
            // 没有引号，不支持
            return false;
        }
        
        // 查找结束引号
        const char *end = strchr(attr, quote);
        if (!end) {
            return false;
        }
        
        size_t len = end - attr;
        if (len >= value_size) {
            len = value_size - 1;
        }
        memcpy(value, attr, len);
        value[len] = '\0';
        
        return true;
    }
    
    return false;
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

    // 查找 <metadata> 标签 (支持多种命名空间)
    const char *metadata_start = strstr(parser->content, "<metadata");
    if (!metadata_start) {
        ESP_LOGW(TAG, "metadata tag not found, trying alternative namespaces");
        // 尝试查找其他可能的命名空间
        metadata_start = strstr(parser->content, "<dc:metadata");
        if (!metadata_start) {
            metadata_start = strstr(parser->content, "<opf:metadata");
        }
    }

    if (!metadata_start) {
        ESP_LOGW(TAG, "metadata tag not found in any namespace");
        return false;
    }

    // 查找对应的 </metadata> 标签 (支持命名空间)
    const char *metadata_end = strstr(metadata_start, "</metadata>");
    if (!metadata_end) {
        // 尝试带命名空间的闭合标签
        metadata_end = strstr(metadata_start, "</dc:metadata>");
        if (!metadata_end) {
            metadata_end = strstr(metadata_start, "</opf:metadata>");
        }
    }

    if (!metadata_end) {
        ESP_LOGW(TAG, "metadata end tag not found, scanning for next major tag");
        // 尝试找到下一个主要标签
        const char *candidates[] = {
            strstr(metadata_start, "<manifest"),
            strstr(metadata_start, "<spine"),
            strstr(metadata_start, "<guide"),
            strstr(metadata_start, "</package>"),
            NULL
        };
        
        metadata_end = NULL;
        for (int i = 0; candidates[i] != NULL; i++) {
            if (candidates[i] != NULL && (metadata_end == NULL || candidates[i] < metadata_end)) {
                metadata_end = candidates[i];
            }
        }
        
        if (!metadata_end) {
            ESP_LOGW(TAG, "No next major tag found, using rest of content");
            metadata_end = parser->content + parser->length;
        }
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

// 只计数 spine 项目的函数，用于预先确定需要分配的内存
int epub_xml_count_spine_items(epub_xml_parser_t *parser) {
    if (!parser) {
        return 0;
    }

    // 查找 <spine> 标签
    const char *spine_start = strstr(parser->content, "<spine");
    if (!spine_start) {
        spine_start = strstr(parser->content, "<opf:spine");
    }
    if (!spine_start) {
        ESP_LOGW(TAG, "spine tag not found in OPF, trying case-insensitive search");
        // 尝试搜索spine标签的其他变体
        const char *search = parser->content;
        while ((search = strchr(search, '<')) != NULL) {
            if ((strncmp(search, "<spine", 6) == 0 || strncmp(search, "<opf:spine", 10) == 0 ||
                 strncmp(search, "<Spine", 6) == 0 || strncmp(search, "<SPINE", 6) == 0)) {
                spine_start = search;
                break;
            }
            search++;
        }
        if (!spine_start) {
            ESP_LOGE(TAG, "spine tag not found in OPF content (length=%zu)", parser->length);
            // 打印OPF内容的前500字符用于调试
            char debug_buf[512] = {0};
            strncpy(debug_buf, parser->content, sizeof(debug_buf) - 1);
            ESP_LOGD(TAG, "OPF start: %s", debug_buf);
            return 0;
        }
    }

    // 查找对应的 </spine> 标签
    const char *spine_end = strstr(spine_start, "</spine>");
    if (!spine_end) {
        spine_end = strstr(spine_start, "</opf:spine>");
    }
    if (!spine_end) {
        spine_end = strstr(spine_start, "<manifest");
        if (!spine_end) {
            spine_end = strstr(spine_start, "</package>");
        }
        if (!spine_end) {
            spine_end = parser->content + parser->length;
        }
    }

    int count = 0;
    const char *itemref_start = spine_start;
    const int max_count = 500;  // 安全上限

    while (count < max_count) {
        // 同时查找 <itemref 和 <opf:itemref
        const char *itemref_pos = strstr(itemref_start, "<itemref");
        const char *opf_itemref_pos = strstr(itemref_start, "<opf:itemref");
        
        // 选择最先出现的
        const char *next_itemref = NULL;
        if (itemref_pos && opf_itemref_pos) {
            next_itemref = (itemref_pos < opf_itemref_pos) ? itemref_pos : opf_itemref_pos;
        } else if (itemref_pos) {
            next_itemref = itemref_pos;
        } else if (opf_itemref_pos) {
            next_itemref = opf_itemref_pos;
        }
        
        if (!next_itemref || next_itemref >= spine_end) {
            break;
        }
        count++;
        const char *itemref_end = strchr(next_itemref, '>');
        if (!itemref_end) break;
        itemref_start = itemref_end + 1;
    }

    ESP_LOGI(TAG, "Found %d itemref tags in spine", count);
    if (count == 0 && spine_start) {
        // 诊断：打印spine标签的内容
        ESP_LOGD(TAG, "spine_start found at offset %u, content sample:", (unsigned)(spine_start - parser->content));
        char debug_buf[256] = {0};
        strncpy(debug_buf, spine_start, sizeof(debug_buf) - 1);
        ESP_LOGD(TAG, "spine content: %s", debug_buf);
    }

    return count;
}

int epub_xml_parse_spine(epub_xml_parser_t *parser,
                         epub_xml_spine_item_t *spine_items,
                         int max_items) {
    if (!parser || !spine_items || max_items <= 0) {
        return 0;
    }

    // 查找 <spine> 标签 (支持命名空间)
    const char *spine_start = strstr(parser->content, "<spine");
    if (!spine_start) {
        ESP_LOGW(TAG, "spine tag not found, trying alternative namespaces");
        // 尝试带命名空间的标签
        spine_start = strstr(parser->content, "<opf:spine");
    }

    if (!spine_start) {
        ESP_LOGE(TAG, "spine tag not found");
        return 0;
    }

    // 查找对应的 </spine> 标签
    const char *spine_end = strstr(spine_start, "</spine>");
    if (!spine_end) {
        // 尝试带命名空间的闭合标签
        spine_end = strstr(spine_start, "</opf:spine>");
    }

    if (!spine_end) {
        ESP_LOGW(TAG, "spine end tag not found, scanning for itemrefs until next major tag");
        // 如果找不到闭合标签，尝试找到下一个主要标签作为边界
        // 按优先级尝试多个可能的边界
        const char *candidates[] = {
            strstr(spine_start, "</spine>"),
            strstr(spine_start, "</opf:spine>"),
            strstr(spine_start, "<manifest"),
            strstr(spine_start, "<opf:manifest"),
            strstr(spine_start, "<guide"),
            strstr(spine_start, "<opf:guide"),
            strstr(spine_start, "</package>"),
            NULL
        };
        
        spine_end = NULL;
        for (int i = 0; candidates[i] != NULL; i++) {
            if (candidates[i] != NULL && (spine_end == NULL || candidates[i] < spine_end)) {
                spine_end = candidates[i];
            }
        }
        
        if (!spine_end) {
            spine_end = parser->content + parser->length;
        }
    }

    int count = 0;
    const char *itemref_start = spine_start;

    while (count < max_items) {
        // 同时查找 <itemref 和 <opf:itemref
        const char *itemref_pos = strstr(itemref_start, "<itemref");
        const char *opf_itemref_pos = strstr(itemref_start, "<opf:itemref");
        
        // 选择最先出现的
        const char *next_itemref = NULL;
        if (itemref_pos && opf_itemref_pos) {
            next_itemref = (itemref_pos < opf_itemref_pos) ? itemref_pos : opf_itemref_pos;
        } else if (itemref_pos) {
            next_itemref = itemref_pos;
        } else if (opf_itemref_pos) {
            next_itemref = opf_itemref_pos;
        }
        
        if (!next_itemref || next_itemref >= spine_end) {
            break;
        }

        const char *itemref_end = strchr(next_itemref, '>');
        if (!itemref_end) break;

        // 提取 idref 属性
        epub_xml_spine_item_t *item = &spine_items[count];
        if (extract_attribute(next_itemref, "idref", item->idref, sizeof(item->idref))) {
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

    // 查找 <manifest> 标签 (支持命名空间)
    const char *manifest_start = strstr(parser->content, "<manifest");
    if (!manifest_start) {
        ESP_LOGW(TAG, "manifest tag not found, trying alternative namespaces");
        manifest_start = strstr(parser->content, "<opf:manifest");
    }

    if (!manifest_start) {
        ESP_LOGE(TAG, "manifest tag not found");
        return false;
    }

    // 查找对应的 </manifest> 标签
    const char *manifest_end = strstr(manifest_start, "</manifest>");
    if (!manifest_end) {
        manifest_end = strstr(manifest_start, "</opf:manifest>");
    }

    if (!manifest_end) {
        ESP_LOGW(TAG, "manifest end tag not found, searching in rest of content");
        manifest_end = parser->content + parser->length;
    }

    // 查找匹配的 <item> 标签 (尝试多种属性格式)
    char search_tag[128];
    snprintf(search_tag, sizeof(search_tag), "id=\"%s\"", idref);

    const char *item_start = strstr(manifest_start, search_tag);
    if (!item_start || item_start >= manifest_end) {
        // 尝试单引号格式
        snprintf(search_tag, sizeof(search_tag), "id='%s'", idref);
        item_start = strstr(manifest_start, search_tag);
    }

    if (!item_start || item_start >= manifest_end) {
        ESP_LOGW(TAG, "manifest item with id='%s' not found", idref);
        return false;
    }

    // 提取 href 属性
    if (extract_attribute(item_start, "href", href, href_size)) {
        return true;
    }

    // 尝试带命名空间的 href 属性
    if (extract_attribute(item_start, "xlink:href", href, href_size)) {
        return true;
    }

    ESP_LOGW(TAG, "href attribute not found for id='%s'", idref);
    return false;
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
