/**
 * @file epub_xml.cpp
 * @brief EPUB XML 解析器实现 - 使用 TinyXML2 进行可靠解析
 */

#include "epub_xml.h"
#include "tinyxml2.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EPUB_XML";

static bool is_valid_utf8_sequence(const char *data, size_t len, size_t *consumed) {
    if (len == 0) {
        return false;
    }

    unsigned char lead = (unsigned char)data[0];
    if (lead < 0x80) {
        *consumed = 1;
        return true;
    }

    size_t seq_len;
    if ((lead & 0xE0) == 0xC0) {
        if (lead < 0xC2) {
            return false;
        }
        seq_len = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        seq_len = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        if (lead > 0xF4) {
            return false;
        }
        seq_len = 4;
    } else {
        return false;
    }

    if (len < seq_len) {
        return false;
    }

    for (size_t i = 1; i < seq_len; ++i) {
        unsigned char c = (unsigned char)data[i];
        if ((c & 0xC0) != 0x80) {
            return false;
        }
    }

    *consumed = seq_len;
    return true;
}

static void log_xml_lines_window(const char *xml, size_t len, int center_line, int radius) {
    if (!xml || len == 0 || center_line <= 0 || radius < 0) {
        return;
    }

    int start_line = center_line - radius;
    if (start_line < 1) {
        start_line = 1;
    }
    int end_line = center_line + radius;

    int current_line = 1;
    size_t pos = 0;

    ESP_LOGE(TAG, "XML around error line %d:", center_line);
    while (pos < len && current_line <= end_line) {
        size_t line_end = pos;
        while (line_end < len && xml[line_end] != '\n') {
            line_end++;
        }

        if (current_line >= start_line) {
            char line_buf[256] = {0};
            size_t line_len = line_end - pos;
            if (line_len > 220) {
                line_len = 220;
            }
            memcpy(line_buf, xml + pos, line_len);
            ESP_LOGE(TAG, "  L%d: %s", current_line, line_buf);
        }

        pos = (line_end < len) ? (line_end + 1) : line_end;
        current_line++;
    }
}

static char *sanitize_xml_for_tinyxml2(const char *input, size_t input_len, size_t *out_len) {
    if (!input || input_len == 0) {
        return nullptr;
    }

    // 1) Strip UTF-8 BOM if present
    size_t start = 0;
    if (input_len >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF) {
        start = 3;
    }

    // 2) Remove control chars (except \t/\n/\r) and drop invalid UTF-8 bytes.
    //    This avoids breaking attributes with embedded NUL / control bytes.
    char *stage1 = (char *)malloc((input_len - start) + 1);
    if (!stage1) {
        return nullptr;
    }
    size_t w = 0;
    size_t r = start;
    while (r < input_len) {
        unsigned char c = (unsigned char)input[r];
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
            r++;
            continue;
        }

        size_t remaining = input_len - r;
        size_t consumed = 0;
        if (!is_valid_utf8_sequence(input + r, remaining, &consumed)) {
            r++;
            continue;
        }

        memcpy(stage1 + w, input + r, consumed);
        w += consumed;
        r += consumed;
    }
    stage1[w] = '\0';

    // 3) Remove XML comments safely: <!-- ... -->
    //    If a comment is unterminated, truncate at comment start.
    size_t len = w;
    char *p = stage1;
    while (true) {
        char *cstart = strstr(p, "<!--");
        if (!cstart) {
            break;
        }
        char *cend = strstr(cstart + 4, "-->");
        if (!cend) {
            // Unterminated comment => truncate
            *cstart = '\0';
            len = (size_t)(cstart - stage1);
            break;
        }
        cend += 3;
        memmove(cstart, cend, (stage1 + len) - cend + 1);
        len -= (size_t)(cend - cstart);
        p = cstart;
    }

    if (out_len) {
        *out_len = len;
    }
    return stage1;
}

static char *truncate_after_spine_and_close_package(const char *input, size_t input_len, size_t *out_len) {
    if (!input || input_len == 0) {
        return nullptr;
    }

    const char *spine_close = strstr(input, "</spine>");
    if (!spine_close) {
        return nullptr;
    }

    const char *after_spine = spine_close + strlen("</spine>");
    const char *package_close = strstr(after_spine, "</package>");
    if (package_close) {
        size_t keep_len = (package_close + strlen("</package>")) - input;
        char *out = (char *)malloc(keep_len + 2);
        if (!out) {
            return nullptr;
        }
        memcpy(out, input, keep_len);
        out[keep_len] = '\n';
        out[keep_len + 1] = '\0';
        if (out_len) {
            *out_len = keep_len + 1;
        }
        return out;
    }

    // No </package>; synthesize one.
    const char *closing = "\n</package>\n";
    size_t closing_len = strlen(closing);
    size_t keep_len = (size_t)(after_spine - input);

    char *out = (char *)malloc(keep_len + closing_len + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, input, keep_len);
    memcpy(out + keep_len, closing, closing_len);
    out[keep_len + closing_len] = '\0';
    if (out_len) {
        *out_len = keep_len + closing_len;
    }
    return out;
}

struct epub_xml_parser {
    char *content;
    size_t length;
    tinyxml2::XMLDocument *doc;
};

extern "C" {

epub_xml_parser_t* epub_xml_create(const char *xml_content, size_t content_length) {
    if (!xml_content || content_length == 0) {
        ESP_LOGE(TAG, "Invalid XML content");
        return NULL;
    }

    epub_xml_parser_t *parser = (epub_xml_parser_t*)calloc(1, sizeof(epub_xml_parser_t));
    if (!parser) {
        ESP_LOGE(TAG, "Failed to allocate parser");
        return NULL;
    }

    parser->content = (char*)malloc(content_length + 1);
    if (!parser->content) {
        ESP_LOGE(TAG, "Failed to allocate content buffer");
        free(parser);
        return NULL;
    }
    
    memcpy(parser->content, xml_content, content_length);
    parser->content[content_length] = '\0';
    parser->length = content_length;

    // 调试：打印原始 XML 的前 2000 字节
    ESP_LOGW(TAG, "=== RAW XML DATA (first 2000 bytes) ===");
    for (size_t i = 0; i < content_length && i < 2000; i += 100) {
        char line_buf[120] = {0};
        size_t copy_len = (i + 100 <= content_length) ? 100 : (content_length - i);
        if (copy_len > 100) {
            copy_len = 100;
        }
        memcpy(line_buf, parser->content + i, copy_len);
        ESP_LOGW(TAG, "  [%04zu] %s", i, line_buf);
    }
    ESP_LOGW(TAG, "=== END RAW XML ===");

    // 先按“示例库”的方式：原样交给 tinyxml2 解析。
    parser->doc = new tinyxml2::XMLDocument(true, tinyxml2::COLLAPSE_WHITESPACE);
    if (!parser->doc) {
        ESP_LOGE(TAG, "Failed to create XMLDocument");
        free(parser->content);
        free(parser);
        return NULL;
    }

    tinyxml2::XMLError err = parser->doc->Parse(parser->content, parser->length);
    if (err == tinyxml2::XML_SUCCESS) {
        ESP_LOGI(TAG, "XML parsed successfully (raw)");
        return parser;
    }

    ESP_LOGW(TAG, "Raw XML parse failed: %s", parser->doc->ErrorName());
    ESP_LOGW(TAG, "Error ID: %d, Line: %d", err, parser->doc->ErrorLineNum());
    log_xml_lines_window(parser->content, parser->length, parser->doc->ErrorLineNum(), 3);

    // 回退：做一轮“安全清洗”并重试解析。
    delete parser->doc;
    parser->doc = nullptr;

    size_t sanitized_len = 0;
    char *sanitized = sanitize_xml_for_tinyxml2(parser->content, parser->length, &sanitized_len);
    if (!sanitized || sanitized_len == 0) {
        ESP_LOGE(TAG, "Failed to sanitize XML");
        free(parser->content);
        free(parser);
        return NULL;
    }

    // 再回退：如果尾部有垃圾数据导致解析失败，截断到 </spine> 并补齐 </package>。
    // 仅在第二次解析失败时才使用。
    parser->doc = new tinyxml2::XMLDocument(true, tinyxml2::COLLAPSE_WHITESPACE);
    if (!parser->doc) {
        ESP_LOGE(TAG, "Failed to create XMLDocument");
        free(sanitized);
        free(parser->content);
        free(parser);
        return NULL;
    }

    err = parser->doc->Parse(sanitized, sanitized_len);
    if (err == tinyxml2::XML_SUCCESS) {
        free(parser->content);
        parser->content = sanitized;
        parser->length = sanitized_len;
        ESP_LOGI(TAG, "XML parsed successfully (sanitized)");
        return parser;
    }

    ESP_LOGW(TAG, "Sanitized XML parse failed: %s", parser->doc->ErrorName());
    ESP_LOGW(TAG, "Error ID: %d, Line: %d", err, parser->doc->ErrorLineNum());
    log_xml_lines_window(sanitized, sanitized_len, parser->doc->ErrorLineNum(), 3);

    delete parser->doc;
    parser->doc = nullptr;

    size_t truncated_len = 0;
    char *truncated = truncate_after_spine_and_close_package(sanitized, sanitized_len, &truncated_len);
    if (!truncated || truncated_len == 0) {
        ESP_LOGE(TAG, "Failed to recover XML (truncate after spine)");
        free(sanitized);
        free(parser->content);
        free(parser);
        return NULL;
    }

    parser->doc = new tinyxml2::XMLDocument(true, tinyxml2::COLLAPSE_WHITESPACE);
    if (!parser->doc) {
        ESP_LOGE(TAG, "Failed to create XMLDocument");
        free(truncated);
        free(sanitized);
        free(parser->content);
        free(parser);
        return NULL;
    }

    err = parser->doc->Parse(truncated, truncated_len);
    if (err != tinyxml2::XML_SUCCESS) {
        ESP_LOGE(TAG, "Failed to parse XML after recovery: %s", parser->doc->ErrorName());
        ESP_LOGE(TAG, "Error ID: %d, Line: %d", err, parser->doc->ErrorLineNum());
        log_xml_lines_window(truncated, truncated_len, parser->doc->ErrorLineNum(), 3);

        delete parser->doc;
        free(truncated);
        free(sanitized);
        free(parser->content);
        free(parser);
        return NULL;
    }

    free(parser->content);
    free(sanitized);
    parser->content = truncated;
    parser->length = truncated_len;
    ESP_LOGI(TAG, "XML parsed successfully (recovered/truncated)");
    return parser;
}

void epub_xml_destroy(epub_xml_parser_t *parser) {
    if (!parser) return;
    
    if (parser->doc) {
        delete parser->doc;
    }
    if (parser->content) {
        free(parser->content);
    }
    free(parser);
}

bool epub_xml_parse_metadata(epub_xml_parser_t *parser, epub_xml_metadata_t *metadata) {
    if (!parser || !parser->doc || !metadata) {
        return false;
    }

    memset(metadata, 0, sizeof(epub_xml_metadata_t));

    // 查找 package 元素
    tinyxml2::XMLElement *package = parser->doc->FirstChildElement("package");
    if (!package) {
        ESP_LOGW(TAG, "package element not found");
        return false;
    }

    // 查找 metadata 元素
    tinyxml2::XMLElement *metadata_elem = package->FirstChildElement("metadata");
    if (!metadata_elem) {
        ESP_LOGW(TAG, "metadata element not found");
        return false;
    }

    // 提取标题 (dc:title)
    tinyxml2::XMLElement *title = metadata_elem->FirstChildElement("dc:title");
    if (!title) {
        title = metadata_elem->FirstChildElement("title");
    }
    if (title && title->GetText()) {
        strncpy(metadata->title, title->GetText(), sizeof(metadata->title) - 1);
    }

    // 提取作者 (dc:creator)
    tinyxml2::XMLElement *creator = metadata_elem->FirstChildElement("dc:creator");
    if (!creator) {
        creator = metadata_elem->FirstChildElement("creator");
    }
    if (creator && creator->GetText()) {
        strncpy(metadata->author, creator->GetText(), sizeof(metadata->author) - 1);
    }

    // 提取语言 (dc:language)
    tinyxml2::XMLElement *language = metadata_elem->FirstChildElement("dc:language");
    if (!language) {
        language = metadata_elem->FirstChildElement("language");
    }
    if (language && language->GetText()) {
        strncpy(metadata->language, language->GetText(), sizeof(metadata->language) - 1);
    }

    ESP_LOGI(TAG, "Metadata: title='%s', author='%s'", metadata->title, metadata->author);
    return true;
}

int epub_xml_parse_spine(epub_xml_parser_t *parser,
                         epub_xml_spine_item_t *spine_items,
                         int max_items) {
    if (!parser || !parser->doc || !spine_items || max_items <= 0) {
        return 0;
    }

    // 查找 package 元素
    tinyxml2::XMLElement *package = parser->doc->FirstChildElement("package");
    if (!package) {
        ESP_LOGE(TAG, "package element not found");
        return 0;
    }

    // 查找 spine 元素
    tinyxml2::XMLElement *spine = package->FirstChildElement("spine");
    if (!spine) {
        ESP_LOGE(TAG, "spine element not found");
        return 0;
    }

    // 遍历 itemref 元素
    int count = 0;
    tinyxml2::XMLElement *itemref = spine->FirstChildElement("itemref");
    while (itemref && count < max_items) {
        const char *idref = itemref->Attribute("idref");
        if (idref) {
            epub_xml_spine_item_t *item = &spine_items[count];
            strncpy(item->idref, idref, sizeof(item->idref) - 1);
            item->idref[sizeof(item->idref) - 1] = '\0';
            item->index = count;
            item->href[0] = '\0';  // 稍后从 manifest 填充
            count++;
        }
        itemref = itemref->NextSiblingElement("itemref");
    }

    ESP_LOGI(TAG, "Parsed %d spine items", count);
    return count;
}

bool epub_xml_find_manifest_item(epub_xml_parser_t *parser,
                                  const char *idref,
                                  char *href,
                                  size_t href_size) {
    if (!parser || !parser->doc || !idref || !href || href_size == 0) {
        return false;
    }

    // 查找 package 元素
    tinyxml2::XMLElement *package = parser->doc->FirstChildElement("package");
    if (!package) {
        ESP_LOGE(TAG, "package element not found");
        return false;
    }

    // 查找 manifest 元素
    tinyxml2::XMLElement *manifest = package->FirstChildElement("manifest");
    if (!manifest) {
        ESP_LOGE(TAG, "manifest element not found");
        return false;
    }

    // 遍历 item 元素查找匹配的 id
    tinyxml2::XMLElement *item = manifest->FirstChildElement("item");
    while (item) {
        const char *id = item->Attribute("id");
        if (id && strcmp(id, idref) == 0) {
            const char *item_href = item->Attribute("href");
            if (item_href) {
                strncpy(href, item_href, href_size - 1);
                href[href_size - 1] = '\0';
                return true;
            }
        }
        item = item->NextSiblingElement("item");
    }

    ESP_LOGW(TAG, "manifest item with id='%s' not found", idref);
    return false;
}

int epub_xml_count_spine_items(epub_xml_parser_t *parser) {
    if (!parser || !parser->doc) {
        return 0;
    }

    // 查找 package 元素
    tinyxml2::XMLElement *package = parser->doc->FirstChildElement("package");
    if (!package) {
        return 0;
    }

    // 查找 spine 元素
    tinyxml2::XMLElement *spine = package->FirstChildElement("spine");
    if (!spine) {
        return 0;
    }

    // 计数 itemref 元素
    int count = 0;
    tinyxml2::XMLElement *itemref = spine->FirstChildElement("itemref");
    while (itemref) {
        count++;
        itemref = itemref->NextSiblingElement("itemref");
    }

    ESP_LOGI(TAG, "Found %d spine items", count);
    return count;
}

} // extern "C"
