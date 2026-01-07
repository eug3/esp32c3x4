/**
 * @file reader_screen_simple.c
 * @brief 阅读器屏幕实现 - 支持 TXT 和 EPUB
 */

#include "reader_screen_simple.h"
#include "display_engine.h"
#include "input_handler.h"
#include "screen_manager.h"
#include "fonts.h"
#include "txt_reader.h"
#include "epub_parser.h"
#include "xt_eink_font_impl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "EPD_4in26.h"
#include "DEV_Config.h"
#include "wallpaper_manager.h"
#include "gb18030_conv.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "READER_SCREEN";

// 电源按钮引脚
#define BTN_POWER_GPIO GPIO_NUM_3

// Line buffer size for text rendering
#define MAX_LINE_BUFFER_SIZE 512

// EPUB 章节内翻页：保留少量历史，支持左键回退
#define EPUB_PAGE_HISTORY_DEPTH 24

// 缓存窗口配置
#define CACHE_WINDOW_BEFORE 2   // 当前页之前缓存页数
#define CACHE_WINDOW_AFTER  5   // 当前页之后缓存页数
#define MAX_CACHE_PAGES     10  // 最大缓存页数（避免占满 Flash）

// TXT 滑动缓存（按“字/字符”计数）
// 需求：缓存 4000 字；当光标移动到 3000 之后，重建缓存并让光标回到 1000 左右。
#define TXT_CACHE_CHARS 4000
#define TXT_CACHE_RECACHE_THRESHOLD 3000
#define TXT_CACHE_CURSOR_RESET 1000

#define TXT_CACHE_HISTORY_DEPTH 16

static screen_t g_reader_screen = {0};

// 阅读器状态
typedef enum {
    READER_TYPE_NONE,
    READER_TYPE_TXT,
    READER_TYPE_EPUB
} reader_type_t;

static struct {
    reader_type_t type;
    char file_path[256];
    txt_reader_t txt_reader;
    epub_reader_t epub_reader;  // EPUB 阅读器实例
    char epub_html[4096];        // EPUB 章节原始 HTML/XHTML
    char current_text[4096];     // 当前页文本
    int current_page;
    int total_pages;
    int chars_per_page;
    bool is_loaded;

    // EPUB 章节内翻页状态（按章节 HTML 文件字节偏移）
    struct {
        long html_offset;
        int last_html_consumed;
        long history[EPUB_PAGE_HISTORY_DEPTH];
        int history_len;
    } epub_page;

    // TXT 缓存状态（flash-backed）
    struct {
        bool ready;
        char cache_path[128];
        FILE *fp;
        // 每个字符对应的“源文件 byte offset”（字符开始位置）
        int32_t src_pos[TXT_CACHE_CHARS + 1];
        // 每个字符在缓存文件内的 byte offset（前缀和）
        uint32_t cache_off[TXT_CACHE_CHARS + 1];
        uint16_t cached_chars;
        uint16_t cursor; // 当前页起始字符（在缓存内）
        int last_page_consumed_chars; // 最近一次渲染占用的字符数

        int32_t history_src_pos[TXT_CACHE_HISTORY_DEPTH];
        int history_len;
    } txt_cache;
} s_reader_state = {
    .type = READER_TYPE_NONE,
    .epub_reader = {{0}},  // 嵌套结构体需要双重大括号
    .current_page = 0,
    .total_pages = 0,
    .chars_per_page = 600,  // 每页约600字符 (适合480x800屏幕)
    .is_loaded = false,
    .epub_page = {0},
};

static bool epub_fill_current_page_text(void)
{
    if (!s_reader_state.is_loaded || s_reader_state.type != READER_TYPE_EPUB) {
        return false;
    }

    // 读一段“渲染后的纯文本”（从章节内 offset 开始，offset 基于纯文本字节数）
    int bytes_read = epub_parser_read_chapter_text_at(&s_reader_state.epub_reader,
                                                      s_reader_state.current_page - 1,
                                                      s_reader_state.epub_page.html_offset,
                                                      s_reader_state.epub_html,
                                                      sizeof(s_reader_state.epub_html));

    if (bytes_read <= 0) {
        s_reader_state.epub_html[0] = '\0';
        s_reader_state.current_text[0] = '\0';
        s_reader_state.epub_page.last_html_consumed = 0;
        return false;
    }

    // 根据屏幕尺寸/字体，计算本页实际能显示多少字节，并把分页后的文本写入 current_text
    sFONT *ui_font = display_get_default_ascii_font();
    int chinese_font_height = xt_eink_font_get_height();
    if (chinese_font_height == 0) {
        chinese_font_height = 25;
    }
    int line_spacing = 4;
    int font_height = chinese_font_height + line_spacing;
    int max_lines = (SCREEN_HEIGHT - 20) / font_height;
    int max_width = SCREEN_WIDTH - 20;

    size_t out_len = 0;
    int consumed = 0;
    int lines = 0;
    char line[MAX_LINE_BUFFER_SIZE];
    int line_bytes = 0;
    const char *p = s_reader_state.epub_html;
    const char *end = s_reader_state.epub_html + bytes_read;

    s_reader_state.current_text[0] = '\0';

    while (p < end && lines < max_lines && out_len + 2 < sizeof(s_reader_state.current_text)) {
        if (*p == '\n') {
            // flush line
            if (line_bytes > 0) {
                size_t copy = (size_t)line_bytes;
                if (out_len + copy + 2 >= sizeof(s_reader_state.current_text)) {
                    break;
                }
                memcpy(s_reader_state.current_text + out_len, line, copy);
                out_len += copy;
            }
            s_reader_state.current_text[out_len++] = '\n';
            s_reader_state.current_text[out_len] = '\0';
            p++;
            consumed++;
            line_bytes = 0;
            lines++;
            continue;
        }

        uint32_t unicode;
        int char_bytes = xt_eink_font_utf8_to_utf32(p, &unicode);
        if (char_bytes <= 0 || p + char_bytes > end) {
            // bad byte; skip
            p++;
            consumed++;
            continue;
        }

        if (line_bytes + char_bytes >= (int)sizeof(line) - 1) {
            // line buffer full, flush as a line
            if (line_bytes > 0) {
                size_t copy = (size_t)line_bytes;
                if (out_len + copy + 2 >= sizeof(s_reader_state.current_text)) {
                    break;
                }
                memcpy(s_reader_state.current_text + out_len, line, copy);
                out_len += copy;
                s_reader_state.current_text[out_len++] = '\n';
                s_reader_state.current_text[out_len] = '\0';
                lines++;
                line_bytes = 0;
                if (lines >= max_lines) {
                    break;
                }
                continue;
            }
        }

        memcpy(line + line_bytes, p, (size_t)char_bytes);
        line_bytes += char_bytes;
        line[line_bytes] = '\0';

        int w = display_get_text_width_font(line, ui_font);
        if (w > max_width && line_bytes > char_bytes) {
            // flush line without this char
            line_bytes -= char_bytes;
            line[line_bytes] = '\0';

            if (line_bytes > 0) {
                size_t copy = (size_t)line_bytes;
                if (out_len + copy + 2 >= sizeof(s_reader_state.current_text)) {
                    break;
                }
                memcpy(s_reader_state.current_text + out_len, line, copy);
                out_len += copy;
            }
            s_reader_state.current_text[out_len++] = '\n';
            s_reader_state.current_text[out_len] = '\0';
            lines++;
            if (lines >= max_lines) {
                // do NOT consume current char; it will be on next page
                break;
            }

            // start new line with this char
            memcpy(line, p, (size_t)char_bytes);
            line_bytes = char_bytes;
            line[line_bytes] = '\0';

            p += char_bytes;
            consumed += char_bytes;
        } else {
            p += char_bytes;
            consumed += char_bytes;
        }
    }

    // flush remaining line if any and we still have space
    if (lines < max_lines && line_bytes > 0 && out_len + (size_t)line_bytes + 1 < sizeof(s_reader_state.current_text)) {
        memcpy(s_reader_state.current_text + out_len, line, (size_t)line_bytes);
        out_len += (size_t)line_bytes;
        s_reader_state.current_text[out_len] = '\0';
    }

    if (consumed <= 0) {
        // fallback: avoid infinite loop
        consumed = 1;
    }
    s_reader_state.epub_page.last_html_consumed = consumed;
    return true;
}

static uint32_t fnv1a32_str_local(const char *s)
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

static void ensure_littlefs_cache_dir(void)
{
    // 目录不存在就创建；忽略已存在错误
    (void)mkdir("/littlefs", 0755);
    (void)mkdir("/littlefs/txt_cache", 0755);
}

static void txt_cache_close(void)
{
    if (s_reader_state.txt_cache.fp != NULL) {
        fclose(s_reader_state.txt_cache.fp);
        s_reader_state.txt_cache.fp = NULL;
    }
    s_reader_state.txt_cache.ready = false;
    s_reader_state.txt_cache.cached_chars = 0;
    s_reader_state.txt_cache.cursor = 0;
    s_reader_state.txt_cache.last_page_consumed_chars = 0;
}

static int read_next_char_utf8(txt_reader_t *reader, char out_utf8[4], uint8_t *out_utf8_len, int32_t *out_src_start)
{
    if (reader == NULL || !reader->is_open || out_utf8 == NULL || out_utf8_len == NULL || out_src_start == NULL) {
        return 0;
    }

    while (1) {
        int32_t start_pos = (int32_t)reader->position.file_position;
        int c = fgetc(reader->file);
        if (c == EOF) {
            return 0;
        }
        reader->position.file_position++;

        // 跳过 CR
        if (c == '\r') {
            continue;
        }

        *out_src_start = start_pos;

        // 换行
        if (c == '\n') {
            out_utf8[0] = (char)'\n';
            *out_utf8_len = 1;
            return 1;
        }

        // GB18030/GBK：读取 1 或 2 字节并转 UTF-8
        if (reader->encoding == TXT_ENCODING_GB18030) {
            uint8_t raw[2];
            size_t raw_len = 0;
            raw[raw_len++] = (uint8_t)c;

            if (raw[0] >= 0x81 && raw[0] <= 0xFE) {
                int c2 = fgetc(reader->file);
                if (c2 == EOF) {
                    // 文件结束，退化为 '?'
                    out_utf8[0] = '?';
                    *out_utf8_len = 1;
                    return 1;
                }
                uint8_t b2 = (uint8_t)c2;

                // 校验第二字节
                if (b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F) {
                    raw[raw_len++] = b2;
                    reader->position.file_position++;
                } else {
                    // 无效 second byte：放回
                    ungetc(c2, reader->file);
                }
            }

            char tmp_out[8];
            int n = gb18030_to_utf8(raw, raw_len, tmp_out, sizeof(tmp_out));
            if (n <= 0) {
                out_utf8[0] = '?';
                *out_utf8_len = 1;
                return 1;
            }

            // 期望单个字符输出长度 <= 4
            if (n > 4) {
                out_utf8[0] = '?';
                *out_utf8_len = 1;
                return 1;
            }

            memcpy(out_utf8, tmp_out, (size_t)n);
            *out_utf8_len = (uint8_t)n;
            return 1;
        }

        // UTF-8/ASCII
        if (c < 0x80) {
            out_utf8[0] = (char)c;
            *out_utf8_len = 1;
            return 1;
        }

        unsigned char first = (unsigned char)c;
        int utf8_len = 0;
        if ((first & 0xE0) == 0xC0) {
            utf8_len = 2;
        } else if ((first & 0xF0) == 0xE0) {
            utf8_len = 3;
        } else if ((first & 0xF8) == 0xF0) {
            utf8_len = 4;
        } else {
            out_utf8[0] = '?';
            *out_utf8_len = 1;
            return 1;
        }

        out_utf8[0] = (char)c;
        int got = 1;
        for (int i = 1; i < utf8_len; i++) {
            int nb = fgetc(reader->file);
            if (nb == EOF) {
                // EOF：退化为 '?'
                out_utf8[0] = '?';
                *out_utf8_len = 1;
                return 1;
            }
            unsigned char cont = (unsigned char)nb;
            if ((cont & 0xC0) != 0x80) {
                // 非 continuation，放回
                ungetc(nb, reader->file);
                out_utf8[0] = '?';
                *out_utf8_len = 1;
                return 1;
            }
            out_utf8[got++] = (char)nb;
            reader->position.file_position++;
        }

        *out_utf8_len = (uint8_t)got;
        return 1;
    }
}

static bool txt_cache_build_at(int32_t start_src_pos, uint16_t cursor_reset)
{
    if (!s_reader_state.is_loaded || s_reader_state.type != READER_TYPE_TXT) {
        return false;
    }

    ensure_littlefs_cache_dir();

    // 生成缓存路径（按文件路径 hash）
    uint32_t h = fnv1a32_str_local(s_reader_state.file_path);
    snprintf(s_reader_state.txt_cache.cache_path, sizeof(s_reader_state.txt_cache.cache_path),
             "/littlefs/txt_cache/txt_%08x.bin", (unsigned)h);

    // 关闭旧缓存句柄
    txt_cache_close();

    // 定位到缓存起点
    if (!txt_reader_seek(&s_reader_state.txt_reader, (long)start_src_pos)) {
        ESP_LOGW(TAG, "txt_cache seek failed: %ld", (long)start_src_pos);
        return false;
    }

    FILE *fp = fopen(s_reader_state.txt_cache.cache_path, "wb+");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open txt cache file: %s", s_reader_state.txt_cache.cache_path);
        return false;
    }

    s_reader_state.txt_cache.fp = fp;
    s_reader_state.txt_cache.cache_off[0] = 0;
    s_reader_state.txt_cache.cached_chars = 0;

    uint32_t off = 0;
    for (uint16_t i = 0; i < TXT_CACHE_CHARS; i++) {
        char utf8[4];
        uint8_t ulen = 0;
        int32_t src_start = 0;
        int ok = read_next_char_utf8(&s_reader_state.txt_reader, utf8, &ulen, &src_start);
        if (ok == 0) {
            break;
        }

        s_reader_state.txt_cache.src_pos[i] = src_start;
        s_reader_state.txt_cache.cache_off[i] = off;

        size_t wn = fwrite(utf8, 1, (size_t)ulen, fp);
        if (wn != (size_t)ulen) {
            ESP_LOGE(TAG, "Write txt cache failed");
            fclose(fp);
            s_reader_state.txt_cache.fp = NULL;
            remove(s_reader_state.txt_cache.cache_path);
            return false;
        }

        off += (uint32_t)ulen;
        s_reader_state.txt_cache.cached_chars++;
    }

    // 结尾哨兵：src_pos[cached_chars] 记录“下一字符开始”的文件位置
    s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cached_chars] = (int32_t)s_reader_state.txt_reader.position.file_position;
    s_reader_state.txt_cache.cache_off[s_reader_state.txt_cache.cached_chars] = off;
    fflush(fp);

    if (cursor_reset > s_reader_state.txt_cache.cached_chars) {
        cursor_reset = s_reader_state.txt_cache.cached_chars;
    }
    s_reader_state.txt_cache.cursor = cursor_reset;
    s_reader_state.txt_cache.last_page_consumed_chars = 0;
    s_reader_state.txt_cache.ready = true;

    // 保持 txt_reader 的 file_position 对齐到当前 cursor（保存进度用）
    (void)txt_reader_seek(&s_reader_state.txt_reader, (long)s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cursor]);

    ESP_LOGD(TAG, "TXT cache built: chars=%u start=%ld cursor=%u path=%s",
             (unsigned)s_reader_state.txt_cache.cached_chars,
             (long)start_src_pos,
             (unsigned)s_reader_state.txt_cache.cursor,
             s_reader_state.txt_cache.cache_path);
    return true;
}

static bool txt_cache_ensure_ready(void)
{
    if (s_reader_state.txt_cache.ready && s_reader_state.txt_cache.fp != NULL) {
        return true;
    }
    int32_t pos = (int32_t)s_reader_state.txt_reader.position.file_position;
    return txt_cache_build_at(pos, 0);
}

static int txt_cache_get_chinese_font_width_cached(void)
{
    static int cached_width = 0;
    if (cached_width > 0) {
        return cached_width;
    }

    int w = 19;  // Default fallback
    xt_eink_glyph_t glyph;
    if (xt_eink_font_get_glyph(0x4E2D, &glyph) && glyph.width > 0) {
        w = glyph.width;
    }

    cached_width = w;
    return cached_width;
}

static inline int txt_cache_fast_char_width(const char *ch, uint32_t len, int ascii_w, int cjk_w)
{
    if (len == 1) {
        unsigned char c = (unsigned char)ch[0];
        if (c < 0x80) {
            if (c == '\t') {
                return ascii_w * 4;
            }
            return ascii_w;
        }
    }

    (void)ch;
    return (cjk_w > 0) ? cjk_w : ascii_w;
}

static int txt_cache_format_current_page(char *out, size_t out_size, int target_lines, int *out_consumed_chars)
{
    if (out_consumed_chars) {
        *out_consumed_chars = 0;
    }

    if (!txt_cache_ensure_ready() || out == NULL || out_size < 2) {
        return -1;
    }

    sFONT *ui_font = display_get_default_ascii_font();
    int max_width = SCREEN_WIDTH - 20;
    int ascii_w = (ui_font != NULL && ui_font->Width > 0) ? (int)ui_font->Width : 8;
    int cjk_w = txt_cache_get_chinese_font_width_cached();

    uint16_t cur = s_reader_state.txt_cache.cursor;
    if (cur >= s_reader_state.txt_cache.cached_chars) {
        out[0] = '\0';
        return 0;
    }

    // 定位到 cache 文件的起点
    uint32_t start_off = s_reader_state.txt_cache.cache_off[cur];
    (void)fseek(s_reader_state.txt_cache.fp, (long)start_off, SEEK_SET);

    size_t written = 0;
    int lines = 0;
    int line_w = 0;
    int consumed = 0;

    while (lines < target_lines && cur + (uint16_t)consumed < s_reader_state.txt_cache.cached_chars && written < out_size - 5) {
        uint16_t idx = (uint16_t)(cur + consumed);
        uint32_t off0 = s_reader_state.txt_cache.cache_off[idx];
        uint32_t off1 = s_reader_state.txt_cache.cache_off[idx + 1];
        uint32_t len = off1 - off0;

        if (len == 0 || len > 4) {
            // 防御：异常数据直接跳过
            consumed++;
            continue;
        }

        char ch[5] = {0};
        size_t rn = fread(ch, 1, (size_t)len, s_reader_state.txt_cache.fp);
        if (rn != (size_t)len) {
            break;
        }
        ch[len] = '\0';

        // 源文本换行
        if (len == 1 && ch[0] == '\n') {
            if (written < out_size - 2) {
                out[written++] = '\n';
                out[written] = '\0';
            }
            lines++;
            line_w = 0;
            consumed++;
            continue;
        }

        int cw = txt_cache_fast_char_width(ch, len, ascii_w, cjk_w);
        if (line_w + cw > max_width && line_w > 0) {
            // 需要换行，但不消耗当前字符
            if (written < out_size - 2) {
                out[written++] = '\n';
                out[written] = '\0';
            }
            lines++;
            line_w = 0;
            // 回退文件读指针：让下一轮重新读这个字符
            (void)fseek(s_reader_state.txt_cache.fp, -(long)len, SEEK_CUR);
            if (lines >= target_lines) {
                break;
            }
            continue;
        }

        if (written + len >= out_size - 1) {
            // 空间不足：回退并结束
            (void)fseek(s_reader_state.txt_cache.fp, -(long)len, SEEK_CUR);
            break;
        }

        memcpy(out + written, ch, len);
        written += len;
        out[written] = '\0';
        line_w += cw;
        consumed++;
    }

    if (out_consumed_chars) {
        *out_consumed_chars = consumed;
    }
    return (int)written;
}

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool load_txt_file(const char *file_path);
static bool load_epub_file(const char *file_path);
static int calculate_chars_per_page(void);
static void display_current_page(void);
static void next_page(void);
static void prev_page(void);
static void save_reading_progress(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 加载 TXT 文件
 */
static bool load_txt_file(const char *file_path)
{
    ESP_LOGI(TAG, "Loading TXT file: %s", file_path);

    // 初始化 TXT reader
    if (!txt_reader_init(&s_reader_state.txt_reader)) {
        ESP_LOGE(TAG, "Failed to initialize TXT reader");
        return false;
    }

    // 打开文件
    if (!txt_reader_open(&s_reader_state.txt_reader, file_path, TXT_ENCODING_AUTO)) {
        ESP_LOGE(TAG, "Failed to open TXT file");
        return false;
    }

    // 加载上次阅读位置
    txt_reader_load_position(&s_reader_state.txt_reader);

    // 初始化 TXT 滑动缓存（以当前 file_position 为起点）
    s_reader_state.txt_cache.history_len = 0;
    (void)txt_cache_build_at((int32_t)s_reader_state.txt_reader.position.file_position, 0);

    // 动态计算每页字符数
    s_reader_state.chars_per_page = calculate_chars_per_page();

    // 计算总页数
    s_reader_state.total_pages = txt_reader_get_total_pages(&s_reader_state.txt_reader,
                                                              s_reader_state.chars_per_page);

    s_reader_state.type = READER_TYPE_TXT;
    s_reader_state.is_loaded = true;
    s_reader_state.current_page = txt_reader_get_position(&s_reader_state.txt_reader).page_number;

    ESP_LOGI(TAG, "TXT loaded: total pages ~%d", s_reader_state.total_pages);

    return true;
}

/**
 * @brief 加载 EPUB 文件
 */
static bool load_epub_file(const char *file_path)
{
    ESP_LOGI(TAG, "Loading EPUB file: %s", file_path);

    // 初始化 EPUB reader
    if (!epub_parser_init(&s_reader_state.epub_reader)) {
        ESP_LOGE(TAG, "Failed to initialize EPUB reader");
        return false;
    }

    // 打开 EPUB
    if (!epub_parser_open(&s_reader_state.epub_reader, file_path)) {
        ESP_LOGE(TAG, "Failed to open EPUB file");
        return false;
    }

    // 获取第一个章节内容
    // 初始化章节内翻页
    s_reader_state.epub_page.html_offset = 0;
    s_reader_state.epub_page.last_html_consumed = 0;
    s_reader_state.epub_page.history_len = 0;

    int bytes_read = epub_parser_read_chapter_text_at(&s_reader_state.epub_reader, 0,
                                                      0,
                                                      s_reader_state.epub_html,
                                                      sizeof(s_reader_state.epub_html));

    if (bytes_read <= 0) {
        ESP_LOGE(TAG, "Failed to read first chapter");
        epub_parser_close(&s_reader_state.epub_reader);
        return false;
    }

    s_reader_state.epub_html[bytes_read] = '\0';
    // 第一次进入章节时，直接用分页逻辑生成 current_text 和 consumed
    (void)epub_fill_current_page_text();

    s_reader_state.type = READER_TYPE_EPUB;
    s_reader_state.is_loaded = true;
    s_reader_state.current_page = 1;
    s_reader_state.total_pages = epub_parser_get_chapter_count(&s_reader_state.epub_reader);

    ESP_LOGI(TAG, "EPUB loaded: total chapters %d", s_reader_state.total_pages);

    return true;
}

/**
 * @brief 计算屏幕能容纳的字符数（基于实际字体和屏幕尺寸）
 */
static int calculate_chars_per_page(void)
{
    int chinese_font_height = xt_eink_font_get_height();
    if (chinese_font_height == 0) {
        chinese_font_height = 25;  // Default fallback
    }

    int chinese_font_width = 19;  // Default fallback
    xt_eink_glyph_t glyph;
    if (xt_eink_font_get_glyph(0x4E2D, &glyph) && glyph.width > 0) {
        chinese_font_width = glyph.width;
    }

    int line_spacing = 4;
    int font_height = chinese_font_height + line_spacing;

    int usable_height = SCREEN_HEIGHT - 20;
    int lines_per_page = usable_height / font_height;

    int max_width = SCREEN_WIDTH - 20;
    int chars_per_line = max_width / chinese_font_width;
    int total_chars = lines_per_page * chars_per_line;

    ESP_LOGI(TAG, "Calculated chars_per_page: %d (lines=%d, chars_per_line=%d, font_width=%d, font_height=%d)",
             total_chars, lines_per_page, chars_per_line, chinese_font_width, font_height);

    return total_chars;
}

/**
 * @brief 显示当前页
 */
static void display_current_page(void)
{
    // 清除屏幕（确保旧内容不残留）
    display_clear(COLOR_WHITE);

    sFONT *ui_font = display_get_default_ascii_font();
    int chinese_font_height = xt_eink_font_get_height();
    if (chinese_font_height == 0) {
        chinese_font_height = 25;  // Default fallback
    }
    int line_spacing = 4;
    int font_height = chinese_font_height + line_spacing;

    // 显示页码
    char page_info[32];
    if (s_reader_state.type == READER_TYPE_TXT) {
        snprintf(page_info, sizeof(page_info), "%d/%d",
                 s_reader_state.current_page + 1, s_reader_state.total_pages);
    } else {
        snprintf(page_info, sizeof(page_info), "Chapter %d/%d",
                 s_reader_state.current_page, s_reader_state.total_pages);
    }

    display_draw_text_font(10, 5, page_info, ui_font, COLOR_BLACK, COLOR_WHITE);

    // 显示文本内容
    if (s_reader_state.type == READER_TYPE_TXT) {
        int target_lines = (SCREEN_HEIGHT - 20) / (xt_eink_font_get_height() + 4);
        int consumed_chars = 0;
        int bytes_written = txt_cache_format_current_page(s_reader_state.current_text,
                                                          sizeof(s_reader_state.current_text),
                                                          target_lines,
                                                          &consumed_chars);

        if (bytes_written < 0) {
            s_reader_state.current_text[0] = '\0';
            s_reader_state.txt_cache.last_page_consumed_chars = 0;
        } else {
            s_reader_state.current_text[sizeof(s_reader_state.current_text) - 1] = '\0';
            s_reader_state.txt_cache.last_page_consumed_chars = consumed_chars;
        }

        ESP_LOGD(TAG, "TXT page render: cache_cursor=%u consumed=%d bytes=%d",
                 (unsigned)s_reader_state.txt_cache.cursor,
                 consumed_chars,
                 bytes_written);

        if (s_reader_state.current_text[0] != '\0') {
            // current_text 已经按宽度插入了换行，这里直接逐行绘制即可
            int y = 20;
            int x = 10;
            const char *p = s_reader_state.current_text;
            char line[MAX_LINE_BUFFER_SIZE];

            while (*p != '\0' && y < SCREEN_HEIGHT) {
                const char *nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                if (len >= sizeof(line)) {
                    len = sizeof(line) - 1;
                }

                if (len > 0) {
                    memcpy(line, p, len);
                    line[len] = '\0';
                    display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
                }

                y += font_height;
                if (!nl) {
                    break;
                }
                p = nl + 1;
            }
        }
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // 先根据当前 offset 生成本页文本（章节内翻页 + 精确消耗字节数）
        (void)epub_fill_current_page_text();

        // current_text 已经按宽度插入了换行，这里直接逐行绘制即可
        int y = 20;
        int x = 10;
        const char *p = s_reader_state.current_text;
        char line[MAX_LINE_BUFFER_SIZE];

        while (*p != '\0' && y < SCREEN_HEIGHT) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            if (len > 0) {
                memcpy(line, p, len);
                line[len] = '\0';
                display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
            }
            y += font_height;
            if (!nl) {
                break;
            }
            p = nl + 1;
        }
    }
}

/**
 * @brief 下一页
 */
static void next_page(void)
{
    if (!s_reader_state.is_loaded) {
        return;
    }

    if (s_reader_state.type == READER_TYPE_TXT) {
        if (!txt_cache_ensure_ready()) {
            return;
        }

        // 记录当前页起点（用于上一页）
        if (s_reader_state.txt_cache.history_len < TXT_CACHE_HISTORY_DEPTH) {
            int32_t cur_src = s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cursor];
            s_reader_state.txt_cache.history_src_pos[s_reader_state.txt_cache.history_len++] = cur_src;
        }

        int step = s_reader_state.txt_cache.last_page_consumed_chars;
        if (step <= 0) {
            step = 1;
        }

        uint16_t new_cursor = (uint16_t)(s_reader_state.txt_cache.cursor + step);

        // 如果已到缓存尾部：从“缓存结尾的源文件位置”继续建新缓存
        if (new_cursor >= s_reader_state.txt_cache.cached_chars) {
            int32_t next_src = s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cached_chars];
            (void)txt_cache_build_at(next_src, 0);
            new_cursor = 0;
        }

        s_reader_state.txt_cache.cursor = new_cursor;

        // 达到阈值后重建缓存，让 cursor 回到 1000 左右
        if (s_reader_state.txt_cache.cursor > TXT_CACHE_RECACHE_THRESHOLD &&
            s_reader_state.txt_cache.cursor >= TXT_CACHE_CURSOR_RESET) {
            uint16_t anchor = (uint16_t)(s_reader_state.txt_cache.cursor - TXT_CACHE_CURSOR_RESET);
            int32_t new_start = s_reader_state.txt_cache.src_pos[anchor];
            (void)txt_cache_build_at(new_start, TXT_CACHE_CURSOR_RESET);
        } else {
            // 同步 txt_reader 位置（保存进度用）
            (void)txt_reader_seek(&s_reader_state.txt_reader, (long)s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cursor]);
        }

        if (s_reader_state.current_page + 1 < s_reader_state.total_pages) {
            s_reader_state.current_page++;
        }
        ESP_LOGI(TAG, "Next page: %d/%d (cursor=%u)",
                 s_reader_state.current_page + 1, s_reader_state.total_pages,
                 (unsigned)s_reader_state.txt_cache.cursor);
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 章节内翻页；到达章节尾部再进入下一章
        if (s_reader_state.epub_page.history_len < EPUB_PAGE_HISTORY_DEPTH) {
            s_reader_state.epub_page.history[s_reader_state.epub_page.history_len++] = s_reader_state.epub_page.html_offset;
        }

        int step = s_reader_state.epub_page.last_html_consumed;
        if (step <= 0) {
            step = (int)sizeof(s_reader_state.epub_html) - 1;
        }
        s_reader_state.epub_page.html_offset += step;

        // 预读一下判断是否到章节末尾
        if (!epub_fill_current_page_text()) {
            // 章节已结束：切换到下一章
            if (s_reader_state.current_page < s_reader_state.total_pages) {
                s_reader_state.current_page++;
                s_reader_state.epub_page.html_offset = 0;
                s_reader_state.epub_page.last_html_consumed = 0;
                s_reader_state.epub_page.history_len = 0;
                (void)epub_fill_current_page_text();
            }
        }

        ESP_LOGI(TAG, "Next page: chapter %d/%d offset=%ld", s_reader_state.current_page, s_reader_state.total_pages, (long)s_reader_state.epub_page.html_offset);
    }
}

/**
 * @brief 上一页
 */
static void prev_page(void)
{
    if (!s_reader_state.is_loaded) {
        return;
    }

    if (s_reader_state.type == READER_TYPE_TXT) {
        if (s_reader_state.txt_cache.history_len > 0) {
            int32_t pos = s_reader_state.txt_cache.history_src_pos[--s_reader_state.txt_cache.history_len];
            (void)txt_cache_build_at(pos, 0);
            if (s_reader_state.current_page > 0) {
                s_reader_state.current_page--;
            }
        }
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 章节内上一页
        if (s_reader_state.epub_page.history_len > 0) {
            s_reader_state.epub_page.html_offset = s_reader_state.epub_page.history[--s_reader_state.epub_page.history_len];
            (void)epub_fill_current_page_text();
        } else if (s_reader_state.current_page > 1) {
            // 若本章无历史（刚进入章开头），回到上一章开头
            s_reader_state.current_page--;
            s_reader_state.epub_page.html_offset = 0;
            s_reader_state.epub_page.last_html_consumed = 0;
            s_reader_state.epub_page.history_len = 0;
            (void)epub_fill_current_page_text();
        }
    }

    ESP_LOGI(TAG, "Prev page: %d/%d", s_reader_state.current_page, s_reader_state.total_pages);
}

/**
 * @brief 保存阅读进度
 */
static void save_reading_progress(void)
{
    if (s_reader_state.is_loaded && s_reader_state.type == READER_TYPE_TXT) {
        // 确保保存的是“当前页起点”的源文件位置
        if (s_reader_state.txt_cache.ready) {
            (void)txt_reader_seek(&s_reader_state.txt_reader, (long)s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cursor]);
        }
        txt_reader_save_position(&s_reader_state.txt_reader);
        ESP_LOGI(TAG, "Reading progress saved");
    }
}

/**********************
 * SCREEN CALLBACKS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Reader screen shown");

    // 初始化中文字体系统
    if (!xt_eink_font_init()) {
        ESP_LOGW(TAG, "Failed to initialize Chinese font, will use fallback rendering");
    }

    const char *file_path = (const char *)screen->user_data;
    if (file_path == NULL) {
        ESP_LOGE(TAG, "No file path specified");
        display_clear(COLOR_WHITE);
        sFONT *ui_font = display_get_default_ascii_font();
        display_draw_text_font(20, 100, "No file specified",
                               ui_font, COLOR_BLACK, COLOR_WHITE);
        return;
    }

    // 复制文件路径
    strncpy(s_reader_state.file_path, file_path, sizeof(s_reader_state.file_path) - 1);
    s_reader_state.file_path[sizeof(s_reader_state.file_path) - 1] = '\0';

    // 根据文件扩展名判断类型
    const char *ext = strrchr(file_path, '.');
    bool loaded = false;

    if (ext != NULL) {
        if (strcasecmp(ext, ".txt") == 0) {
            loaded = load_txt_file(file_path);
        } else if (strcasecmp(ext, ".epub") == 0) {
            loaded = load_epub_file(file_path);
        }
    }

    if (!loaded) {
        ESP_LOGE(TAG, "Failed to load file: %s", file_path);
        display_clear(COLOR_WHITE);
        sFONT *ui_font = display_get_default_ascii_font();
        display_draw_text_font(20, 100, "Failed to load file",
                               ui_font, COLOR_BLACK, COLOR_WHITE);
        display_draw_text_font(20, 150, file_path,
                               ui_font, COLOR_BLACK, COLOR_WHITE);
        return;
    }

    // 显示第一页
    display_current_page();

    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Reader screen hidden");

    // 保存阅读进度
    save_reading_progress();

    // 保存上次阅读信息
    if (s_reader_state.is_loaded && s_reader_state.file_path[0] != '\0') {
        int32_t pos = 0;
        int page = 0;

        if (s_reader_state.type == READER_TYPE_TXT && s_reader_state.txt_cache.ready) {
            pos = s_reader_state.txt_cache.src_pos[s_reader_state.txt_cache.cursor];
            page = s_reader_state.current_page;
        } else if (s_reader_state.type == READER_TYPE_EPUB) {
            // EPUB 使用章节号作为位置标识
            pos = (int32_t)(s_reader_state.current_page - 1);
            page = s_reader_state.current_page;
        }

        txt_reader_save_last_read(s_reader_state.file_path, pos, page);
        ESP_LOGI(TAG, "Saved last read: %s (pos=%ld, page=%d)",
                 s_reader_state.file_path, (long)pos, page);
    }

    // 清理资源
    if (s_reader_state.type == READER_TYPE_TXT) {
        txt_cache_close();
        txt_reader_cleanup(&s_reader_state.txt_reader);
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        epub_parser_close(&s_reader_state.epub_reader);
        epub_parser_cleanup(&s_reader_state.epub_reader);
    }

    s_reader_state.type = READER_TYPE_NONE;
    s_reader_state.is_loaded = false;
}

static void on_draw(screen_t *screen)
{
    // 显示已在 on_show 中完成
}

// 轻度休眠状态
static bool s_light_sleep_active = false;

static void enter_light_sleep(void)
{
    if (s_light_sleep_active) return;

    ESP_LOGI(TAG, "Entering light sleep (show wallpaper)...");
    s_light_sleep_active = true;

    // 保存阅读进度
    save_reading_progress();

    // 显示壁纸
    wallpaper_show();

    // 刷新显示
    display_refresh(REFRESH_MODE_FULL);
}

static void exit_light_sleep(void)
{
    if (!s_light_sleep_active) return;

    ESP_LOGI(TAG, "Exiting light sleep...");
    s_light_sleep_active = false;

    // 清除壁纸显示
    wallpaper_clear();

    // 重新显示当前页
    display_current_page();
    display_refresh(REFRESH_MODE_FULL);
}

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep...");

    // 保存阅读进度
    save_reading_progress();

    // EPD 进入休眠模式
    EPD_4in26_Sleep();
    DEV_Delay_ms(100);

    // 配置电源按钮唤醒
    esp_deep_sleep_enable_gpio_wakeup(BTN_POWER_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);

    // ESP32 进入深度睡眠
    esp_deep_sleep_start();
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    // 轻度休眠时，任意键唤醒
    if (s_light_sleep_active) {
        if (event == BTN_EVENT_PRESSED) {
            ESP_LOGI(TAG, "Waking from light sleep...");
            exit_light_sleep();
        }
        return;
    }

    // 检查是否是电源按钮
    if (btn == BTN_POWER) {
        if (event == BTN_EVENT_DOUBLE_CLICK) {
            // 双击电源键：轻度休眠
            enter_light_sleep();
        } else if (event == BTN_EVENT_LONG_PRESSED) {
            // 长按电源键：深度休眠
            enter_deep_sleep();
        }
        return;
    }

    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    switch (btn) {
        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            // 下一页（上下左右均可翻到下一页）
            next_page();
            display_current_page();
            display_refresh(REFRESH_MODE_PARTIAL);
            break;

        case BTN_LEFT:
        case BTN_VOLUME_UP:
            // 上一页
            prev_page();
            display_current_page();
            display_refresh(REFRESH_MODE_PARTIAL);
            break;

        case BTN_BACK:
            // 返回 (自动保存进度)
            screen_manager_back();
            break;

        default:
            break;
    }
}

/**********************
 *  STATIC FUNCTIONS
 **********************/

void reader_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing reader screen");

    g_reader_screen.name = "reader";
    g_reader_screen.user_data = NULL;
    g_reader_screen.on_show = on_show;
    g_reader_screen.on_hide = on_hide;
    g_reader_screen.on_draw = on_draw;
    g_reader_screen.on_event = on_event;
    g_reader_screen.is_visible = false;
    g_reader_screen.needs_redraw = false;
}

screen_t* reader_screen_get_instance(void)
{
    if (g_reader_screen.name == NULL) {
        reader_screen_init();
    }
    return &g_reader_screen;
}
