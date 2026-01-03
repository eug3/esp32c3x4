/**
 * @file epub_html.c
 * @brief EPUB HTML 解析器实现 - 简化版，提取文本块
 */

#include "epub_html.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "EPUB_HTML";

struct epub_html_parser {
    char *content;
    size_t length;
    size_t pos;
    epub_text_block_t *blocks;
    int block_count;
    int current_block;
};

// HTML 标签到块类型的映射
typedef struct {
    const char *tag;
    epub_text_block_type_t type;
} tag_mapping_t;

static const tag_mapping_t tag_map[] = {
    {"<h1", EPUB_TEXT_BLOCK_HEADING1},
    {"<h2", EPUB_TEXT_BLOCK_HEADING2},
    {"<h3", EPUB_TEXT_BLOCK_HEADING3},
    {"<b>", EPUB_TEXT_BLOCK_BOLD},
    {"<strong>", EPUB_TEXT_BLOCK_BOLD},
    {"<i>", EPUB_TEXT_BLOCK_ITALIC},
    {"<em>", EPUB_TEXT_BLOCK_ITALIC},
    {"<img", EPUB_TEXT_BLOCK_IMAGE},
    {NULL, EPUB_TEXT_BLOCK_NORMAL}
};

// 跳过 HTML 标签
static const char* skip_tag(const char *html) {
    if (*html == '<') {
        const char *end = strchr(html, '>');
        return end ? end + 1 : html + 1;
    }
    return html;
}

// 检查是否是标签开始
static bool is_tag_start(const char *html, const char *tag) {
    size_t tag_len = strlen(tag);
    return strncmp(html, tag, tag_len) == 0;
}

// 查找下一个标签
static const char* find_next_tag(const char *html) {
    return strchr(html, '<');
}

// 去除 HTML 实体
static void decode_html_entities(char *text) {
    // 简化版，只处理常见实体
    struct {
        const char *entity;
        char replacement;
    } entities[] = {
        {"&amp;", '&'},
        {"&lt;", '<'},
        {"&gt;", '>'},
        {"&quot;", '"'},
        {"&apos;", '\''},
        {"&nbsp;", ' '},
        {NULL, '\0'}
    };

    for (int i = 0; entities[i].entity; i++) {
        char *pos;
        while ((pos = strstr(text, entities[i].entity)) != NULL) {
            *pos = entities[i].replacement;
            memmove(pos + 1, pos + strlen(entities[i].entity), strlen(pos + strlen(entities[i].entity)) + 1);
        }
    }
}

// 提取图片 src 属性
static bool extract_image_src(const char *img_tag, char *src, size_t src_size) {
    const char *src_start = strstr(img_tag, "src=");
    if (!src_start) return false;

    src_start += 4;
    while (*src_start == ' ' || *src_start == '=') src_start++;
    if (*src_start == '"') src_start++;

    const char *src_end = strchr(src_start, '"');
    if (!src_end) return false;

    size_t len = src_end - src_start;
    if (len >= src_size) len = src_size - 1;
    memcpy(src, src_start, len);
    src[len] = '\0';

    return true;
}

epub_html_parser_t* epub_html_create(const char *html_content, size_t content_length) {
    epub_html_parser_t *parser = calloc(1, sizeof(epub_html_parser_t));
    if (!parser) {
        ESP_LOGE(TAG, "Failed to allocate parser");
        return NULL;
    }

    parser->content = strndup(html_content, content_length);
    parser->length = content_length;
    parser->pos = 0;
    parser->blocks = NULL;
    parser->block_count = 0;
    parser->current_block = 0;

    ESP_LOGI(TAG, "Created HTML parser, content length: %u", content_length);
    return parser;
}

void epub_html_destroy(epub_html_parser_t *parser) {
    if (!parser) return;
    if (parser->content) {
        free(parser->content);
    }
    if (parser->blocks) {
        free(parser->blocks);
    }
    free(parser);
}

bool epub_html_next_block(epub_html_parser_t *parser, epub_text_block_t *block) {
    if (!parser || !block || parser->pos >= parser->length) {
        return false;
    }

    const char *content = parser->content + parser->pos;
    const char *end = parser->content + parser->length;

    // 跳过空白
    while (content < end && isspace(*content)) {
        content++;
    }

    if (content >= end) {
        return false;
    }

    // 检查是否是标签
    if (*content == '<') {
        // 处理标签
        epub_text_block_type_t type = EPUB_TEXT_BLOCK_NORMAL;

        // 检查特殊标签
        for (int i = 0; tag_map[i].tag != NULL; i++) {
            if (is_tag_start(content, tag_map[i].tag)) {
                type = tag_map[i].type;
                break;
            }
        }

        if (type == EPUB_TEXT_BLOCK_IMAGE) {
            // 提取图片
            const char *tag_end = strchr(content, '>');
            if (!tag_end) {
                parser->pos = parser->length;
                return false;
            }

            char tag_buffer[512];
            size_t tag_len = tag_end - content + 1;
            if (tag_len >= sizeof(tag_buffer)) tag_len = sizeof(tag_buffer) - 1;
            memcpy(tag_buffer, content, tag_len);
            tag_buffer[tag_len] = '\0';

            if (extract_image_src(tag_buffer, block->image_src, sizeof(block->image_src))) {
                block->type = EPUB_TEXT_BLOCK_IMAGE;
                block->text[0] = '\0';
                block->text_length = 0;
                parser->pos = tag_end - parser->content + 1;
                ESP_LOGD(TAG, "Found image: %s", block->image_src);
                return true;
            }
        }

        // 跳过标签
        content = skip_tag(content);
        parser->pos = content - parser->content;
    }

    // 提取文本内容（直到下一个标签或结尾）
    const char *text_start = content;
    const char *text_end = find_next_tag(content);
    if (!text_end) {
        text_end = end;
    }

    // 复制文本
    size_t text_len = text_end - text_start;
    if (text_len > 0) {
        // 去除尾部空白
        while (text_len > 0 && isspace((unsigned char)text_start[text_len - 1])) {
            text_len--;
        }

        if (text_len > 0) {
            if (text_len >= sizeof(block->text)) {
                text_len = sizeof(block->text) - 1;
            }
            memcpy(block->text, text_start, text_len);
            block->text[text_len] = '\0';
            block->text_length = text_len;
            block->type = EPUB_TEXT_BLOCK_NORMAL;

            // 解码 HTML 实体
            decode_html_entities(block->text);

            parser->pos = text_end - parser->content;
            ESP_LOGD(TAG, "Text block: '%s' (len=%u)", block->text, block->text_length);
            return true;
        }
    }

    // 递归查找下一个块
    parser->pos = text_end ? text_end - parser->content : parser->length;
    return epub_html_next_block(parser, block);
}

void epub_html_reset(epub_html_parser_t *parser) {
    if (!parser) return;
    parser->current_block = 0;
}

int epub_html_get_block_count(epub_html_parser_t *parser) {
    if (!parser) return 0;

    // 如果还没统计，先统计一次
    if (parser->blocks == NULL) {
        // 临时统计
        int count = 0;
        epub_text_block_t block;
        size_t old_pos = parser->pos;

        parser->pos = 0;
        while (epub_html_next_block(parser, &block)) {
            count++;
        }

        parser->pos = old_pos;
        return count;
    }

    return parser->block_count;
}

bool epub_html_goto_block(epub_html_parser_t *parser, int block_index) {
    if (!parser || block_index < 0) {
        return false;
    }

    parser->pos = 0;
    for (int i = 0; i < block_index; i++) {
        epub_text_block_t block;
        if (!epub_html_next_block(parser, &block)) {
            return false;
        }
    }

    parser->current_block = block_index;
    return true;
}
