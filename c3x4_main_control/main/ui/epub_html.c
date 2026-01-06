/**
 * @file epub_html.c
 * @brief EPUB HTML 解析器实现 - 简化版，提取文本块
 */

#include "epub_html.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char *TAG = "EPUB_HTML";

static size_t append_byte(char *out, size_t out_size, size_t out_len, char c)
{
    if (out == NULL || out_size == 0) {
        return out_len;
    }
    if (out_len + 1 >= out_size) {
        return out_len;
    }
    out[out_len++] = c;
    out[out_len] = '\0';
    return out_len;
}

static bool ends_with_newline(const char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }
    return out[out_len - 1] == '\n';
}

static size_t append_newline(char *out, size_t out_size, size_t out_len)
{
    if (ends_with_newline(out, out_len)) {
        return out_len;
    }
    return append_byte(out, out_size, out_len, '\n');
}

static size_t append_paragraph_break(char *out, size_t out_size, size_t out_len)
{
    // Ensure one blank line between paragraphs when possible.
    out_len = append_newline(out, out_size, out_len);
    if (out_len + 1 < out_size && out_len >= 1 && out[out_len - 1] == '\n') {
        // Add a second newline if not already present.
        if (out_len >= 2 && out[out_len - 2] == '\n') {
            return out_len;
        }
        out_len = append_byte(out, out_size, out_len, '\n');
    }
    return out_len;
}

static int utf32_to_utf8(uint32_t codepoint, char out[4])
{
    if (codepoint <= 0x7Fu) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FFu) {
        out[0] = (char)(0xC0u | ((codepoint >> 6) & 0x1Fu));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint <= 0xFFFFu) {
        out[0] = (char)(0xE0u | ((codepoint >> 12) & 0x0Fu));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    if (codepoint <= 0x10FFFFu) {
        out[0] = (char)(0xF0u | ((codepoint >> 18) & 0x07u));
        out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (codepoint & 0x3Fu));
        return 4;
    }
    return 0;
}

static bool ascii_ieq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with_ascii_ci(const char *s, const char *prefix)
{
    if (s == NULL || prefix == NULL) {
        return false;
    }
    while (*prefix) {
        if (*s == '\0') {
            return false;
        }
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

static size_t decode_entity_and_advance(const char *in, size_t in_len, size_t i, char *out, size_t out_size, size_t out_len, bool *out_wrote_space)
{
    // in[i] == '&'
    size_t j = i + 1;
    while (j < in_len && j - i <= 16 && in[j] != ';' && in[j] != '<' && in[j] != '&') {
        j++;
    }
    if (j >= in_len || in[j] != ';') {
        // Not a complete entity; emit '&'
        out_len = append_byte(out, out_size, out_len, '&');
        if (out_wrote_space) {
            *out_wrote_space = false;
        }
        return i + 1;
    }

    // Extract entity text between & and ;
    char ent[20];
    size_t ent_len = j - (i + 1);
    if (ent_len >= sizeof(ent)) {
        ent_len = sizeof(ent) - 1;
    }
    memcpy(ent, in + i + 1, ent_len);
    ent[ent_len] = '\0';

    // Common named entities
    if (ascii_ieq(ent, "amp")) {
        out_len = append_byte(out, out_size, out_len, '&');
        if (out_wrote_space) *out_wrote_space = false;
        return j + 1;
    }
    if (ascii_ieq(ent, "lt")) {
        out_len = append_byte(out, out_size, out_len, '<');
        if (out_wrote_space) *out_wrote_space = false;
        return j + 1;
    }
    if (ascii_ieq(ent, "gt")) {
        out_len = append_byte(out, out_size, out_len, '>');
        if (out_wrote_space) *out_wrote_space = false;
        return j + 1;
    }
    if (ascii_ieq(ent, "quot")) {
        out_len = append_byte(out, out_size, out_len, '"');
        if (out_wrote_space) *out_wrote_space = false;
        return j + 1;
    }
    if (ascii_ieq(ent, "apos")) {
        out_len = append_byte(out, out_size, out_len, '\'');
        if (out_wrote_space) *out_wrote_space = false;
        return j + 1;
    }
    if (ascii_ieq(ent, "nbsp")) {
        // Treat as a space
        if (out_wrote_space && *out_wrote_space) {
            return j + 1;
        }
        if (!ends_with_newline(out, out_len) && out_len > 0) {
            out_len = append_byte(out, out_size, out_len, ' ');
            if (out_wrote_space) *out_wrote_space = true;
        }
        return j + 1;
    }

    // Numeric entities: &#123; or &#x1F4A9;
    if (ent[0] == '#') {
        uint32_t codepoint = 0;
        const char *p = ent + 1;
        int base = 10;
        if (*p == 'x' || *p == 'X') {
            base = 16;
            p++;
        }
        bool ok = false;
        while (*p) {
            int v = -1;
            if (*p >= '0' && *p <= '9') {
                v = *p - '0';
            } else if (base == 16 && *p >= 'a' && *p <= 'f') {
                v = 10 + (*p - 'a');
            } else if (base == 16 && *p >= 'A' && *p <= 'F') {
                v = 10 + (*p - 'A');
            } else {
                v = -1;
            }
            if (v < 0 || v >= base) {
                ok = false;
                break;
            }
            ok = true;
            codepoint = codepoint * (uint32_t)base + (uint32_t)v;
            p++;
        }
        if (ok) {
            char u8[4];
            int u8len = utf32_to_utf8(codepoint, u8);
            for (int k = 0; k < u8len; k++) {
                out_len = append_byte(out, out_size, out_len, u8[k]);
            }
            if (out_wrote_space) *out_wrote_space = false;
            return j + 1;
        }
    }

    // Unknown entity: drop it
    return j + 1;
}

size_t epub_html_to_text(const char *html, size_t html_len, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (html == NULL || html_len == 0) {
        return 0;
    }

    bool in_script = false;
    bool in_style = false;
    bool wrote_space = false;
    size_t out_len = 0;

    for (size_t i = 0; i < html_len; ) {
        char c = html[i];

        if (!in_script && !in_style && c == '&') {
            size_t next_i = decode_entity_and_advance(html, html_len, i, out, out_size, out_len, &wrote_space);
            // decode_entity_and_advance does not return updated out_len; redo with a safe approach.
            // NOTE: To keep this function low-risk, we'll update out_len via strlen on small buffers.
            out_len = strnlen(out, out_size);
            i = next_i;
            continue;
        }

        if (c == '<') {
            // Handle comments
            if (i + 3 < html_len && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                size_t j = i + 4;
                while (j + 2 < html_len) {
                    if (html[j] == '-' && html[j + 1] == '-' && html[j + 2] == '>') {
                        j += 3;
                        break;
                    }
                    j++;
                }
                i = j;
                continue;
            }

            // Find tag end
            size_t j = i + 1;
            while (j < html_len && html[j] != '>') {
                j++;
            }
            if (j >= html_len) {
                break;
            }

            // Extract tag content between < and >
            char tag[32];
            size_t k = 0;
            size_t t = i + 1;
            while (t < j && k + 1 < sizeof(tag)) {
                char tc = html[t];
                if (tc == ' ' || tc == '\t' || tc == '\r' || tc == '\n' || tc == '/' ) {
                    break;
                }
                tag[k++] = (char)tolower((unsigned char)tc);
                t++;
            }
            tag[k] = '\0';

            bool is_close = (i + 1 < html_len && html[i + 1] == '/');
            if (is_close) {
                // Re-extract for closing tag
                k = 0;
                t = i + 2;
                while (t < j && k + 1 < sizeof(tag)) {
                    char tc = html[t];
                    if (tc == ' ' || tc == '\t' || tc == '\r' || tc == '\n' || tc == '/' ) {
                        break;
                    }
                    tag[k++] = (char)tolower((unsigned char)tc);
                    t++;
                }
                tag[k] = '\0';
            }

            if (!is_close) {
                if (starts_with_ascii_ci(tag, "script")) {
                    in_script = true;
                } else if (starts_with_ascii_ci(tag, "style")) {
                    in_style = true;
                }
            } else {
                if (starts_with_ascii_ci(tag, "script")) {
                    in_script = false;
                } else if (starts_with_ascii_ci(tag, "style")) {
                    in_style = false;
                }
            }

            if (!in_script && !in_style) {
                if (starts_with_ascii_ci(tag, "br")) {
                    out_len = append_newline(out, out_size, out_len);
                    wrote_space = false;
                } else if (starts_with_ascii_ci(tag, "p") || starts_with_ascii_ci(tag, "div") || starts_with_ascii_ci(tag, "section") || starts_with_ascii_ci(tag, "article")) {
                    // Paragraph-ish blocks
                    out_len = append_paragraph_break(out, out_size, out_len);
                    wrote_space = false;
                } else if (starts_with_ascii_ci(tag, "li")) {
                    out_len = append_newline(out, out_size, out_len);
                    out_len = append_byte(out, out_size, out_len, '-');
                    out_len = append_byte(out, out_size, out_len, ' ');
                    wrote_space = false;
                } else if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
                    out_len = append_paragraph_break(out, out_size, out_len);
                    wrote_space = false;
                }
            }

            i = j + 1;
            continue;
        }

        if (in_script || in_style) {
            i++;
            continue;
        }

        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            // Collapse whitespace; keep explicit newlines we inserted.
            if (!ends_with_newline(out, out_len) && out_len > 0 && !wrote_space) {
                out_len = append_byte(out, out_size, out_len, ' ');
                wrote_space = true;
            }
            i++;
            continue;
        }

        // Avoid leading spaces at start of line
        if (c != '\0') {
            if (ends_with_newline(out, out_len) && c == ' ') {
                i++;
                continue;
            }
            out_len = append_byte(out, out_size, out_len, c);
            wrote_space = false;
        }
        i++;
    }

    // Trim trailing spaces/newlines
    while (out_len > 0 && (out[out_len - 1] == ' ' || out[out_len - 1] == '\n' || out[out_len - 1] == '\r' || out[out_len - 1] == '\t')) {
        out[--out_len] = '\0';
    }

    return out_len;
}

void epub_html_stream_init(epub_html_stream_t *st)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

static bool tag_is_block_break(const char *tag)
{
    if (tag == NULL) {
        return false;
    }
    return starts_with_ascii_ci(tag, "p") ||
           starts_with_ascii_ci(tag, "div") ||
           starts_with_ascii_ci(tag, "section") ||
           starts_with_ascii_ci(tag, "article") ||
           (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6');
}

static bool tag_is_br(const char *tag)
{
    return (tag != NULL && starts_with_ascii_ci(tag, "br"));
}

static bool tag_is_li(const char *tag)
{
    return (tag != NULL && starts_with_ascii_ci(tag, "li"));
}

static size_t stream_flush_entity(epub_html_stream_t *st, char *out, size_t out_size, size_t out_len)
{
    if (st == NULL || !st->in_entity) {
        return out_len;
    }
    st->entity[st->entity_len] = '\0';
    // Reuse decode logic: build a fake "&...;" buffer.
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), "&%s;", st->entity);
    if (n > 0 && (size_t)n < sizeof(tmp)) {
        size_t next_i = decode_entity_and_advance(tmp, (size_t)n, 0, out, out_size, out_len, &st->wrote_space);
        (void)next_i;
        out_len = strnlen(out, out_size);
    }
    st->in_entity = false;
    st->entity_len = 0;
    return out_len;
}

static size_t stream_emit_space(epub_html_stream_t *st, char *out, size_t out_size, size_t out_len)
{
    if (st == NULL) {
        return out_len;
    }
    if (!ends_with_newline(out, out_len) && out_len > 0 && !st->wrote_space) {
        out_len = append_byte(out, out_size, out_len, ' ');
        st->wrote_space = true;
    }
    return out_len;
}

static size_t stream_handle_tag_end(epub_html_stream_t *st, char *out, size_t out_size, size_t out_len)
{
    if (st == NULL) {
        return out_len;
    }
    st->tag_name[st->tag_len] = '\0';

    // Comment end handled separately
    if (!st->tag_is_close) {
        if (starts_with_ascii_ci(st->tag_name, "script")) {
            st->in_script = true;
        } else if (starts_with_ascii_ci(st->tag_name, "style")) {
            st->in_style = true;
        }
    } else {
        if (starts_with_ascii_ci(st->tag_name, "script")) {
            st->in_script = false;
        } else if (starts_with_ascii_ci(st->tag_name, "style")) {
            st->in_style = false;
        }
    }

    if (!st->in_script && !st->in_style) {
        if (tag_is_br(st->tag_name)) {
            out_len = append_newline(out, out_size, out_len);
            st->wrote_space = false;
        } else if (tag_is_li(st->tag_name)) {
            out_len = append_newline(out, out_size, out_len);
            out_len = append_byte(out, out_size, out_len, '-');
            out_len = append_byte(out, out_size, out_len, ' ');
            st->wrote_space = false;
        } else if (tag_is_block_break(st->tag_name)) {
            out_len = append_paragraph_break(out, out_size, out_len);
            st->wrote_space = false;
        }
    }

    st->in_tag = false;
    st->tag_is_close = false;
    st->tag_len = 0;
    return out_len;
}

size_t epub_html_stream_feed(epub_html_stream_t *st,
                             const char *in, size_t in_len,
                             char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (st == NULL || in == NULL || in_len == 0) {
        return 0;
    }

    size_t out_len = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];

        // If inside script/style, ignore everything until we see a closing tag start
        if ((st->in_script || st->in_style) && !st->in_tag) {
            if (c == '<') {
                st->in_tag = true;
                st->tag_is_close = false;
                st->tag_len = 0;
                st->in_comment = false;
                st->comment_state = 0;
            }
            continue;
        }

        // Comment skipping state
        if (st->in_comment) {
            // Look for "-->"
            if (st->comment_state == 0) {
                st->comment_state = (c == '-') ? 1 : 0;
            } else if (st->comment_state == 1) {
                st->comment_state = (c == '-') ? 2 : 0;
            } else if (st->comment_state == 2) {
                if (c == '>') {
                    st->in_comment = false;
                    st->in_tag = false;
                    st->comment_state = 0;
                } else {
                    st->comment_state = (c == '-') ? 2 : 0;
                }
            }
            continue;
        }

        // Entity parsing
        if (st->in_entity) {
            if (c == ';') {
                out_len = stream_flush_entity(st, out, out_size, out_len);
                continue;
            }
            if (c == '<' || c == '&' || (st->entity_len + 1 >= sizeof(st->entity))) {
                // Give up; emit nothing for unknown/partial entity
                st->in_entity = false;
                st->entity_len = 0;
                // Re-handle this character normally
            } else {
                st->entity[st->entity_len++] = c;
                continue;
            }
        }

        if (st->in_tag) {
            // Detect comment start: "<!--"
            if (st->tag_len == 0 && c == '!') {
                st->tag_name[st->tag_len++] = '!';
                continue;
            }
            if (st->tag_len == 1 && st->tag_name[0] == '!' && c == '-') {
                st->tag_name[st->tag_len++] = '-';
                continue;
            }
            if (st->tag_len == 2 && st->tag_name[0] == '!' && st->tag_name[1] == '-' && c == '-') {
                st->in_comment = true;
                st->comment_state = 0;
                continue;
            }

            if (st->tag_len == 0 && c == '/') {
                st->tag_is_close = true;
                continue;
            }

            if (c == '>') {
                out_len = stream_handle_tag_end(st, out, out_size, out_len);
                continue;
            }

            // Capture tag name (up to whitespace or '/')
            if (st->tag_len < (sizeof(st->tag_name) - 1)) {
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/') {
                    // stop collecting
                } else {
                    st->tag_name[st->tag_len++] = (char)tolower((unsigned char)c);
                }
            }
            continue;
        }

        if (c == '<') {
            // entering tag
            st->in_tag = true;
            st->tag_is_close = false;
            st->tag_len = 0;
            st->in_comment = false;
            st->comment_state = 0;
            continue;
        }

        if (c == '&') {
            st->in_entity = true;
            st->entity_len = 0;
            continue;
        }

        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            out_len = stream_emit_space(st, out, out_size, out_len);
            continue;
        }

        // Normal character
        out_len = append_byte(out, out_size, out_len, c);
        st->wrote_space = false;

        if (out_len + 2 >= out_size) {
            break;
        }
    }

    // Don't flush partial entity at chunk end; keep it for next chunk.
    return out_len;
}

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
    while (content < end && isspace((unsigned char)*content)) {
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
