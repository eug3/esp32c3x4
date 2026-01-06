/**
 * @file reader_screen_simple.c
 * @brief 阅读器屏幕实现 - 支持 TXT 和 EPUB
 */

#include "reader_screen_simple.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "fonts.h"
#include "txt_reader.h"
#include "epub_parser.h"
#include "xt_eink_font_impl.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "READER_SCREEN";

// Line buffer size for text rendering
#define MAX_LINE_BUFFER_SIZE 512

// 缓存窗口配置
#define CACHE_WINDOW_BEFORE 2   // 当前页之前缓存页数
#define CACHE_WINDOW_AFTER  5   // 当前页之后缓存页数
#define MAX_CACHE_PAGES     10  // 最大缓存页数（避免占满 Flash）

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
    char current_text[4096];     // 当前页文本
    int current_page;
    int total_pages;
    int chars_per_page;
    bool is_loaded;
} s_reader_state = {
    .type = READER_TYPE_NONE,
    .epub_reader = {{0}},  // 嵌套结构体需要双重大括号
    .current_page = 0,
    .total_pages = 0,
    .chars_per_page = 600,  // 每页约600字符 (适合480x800屏幕)
    .is_loaded = false,
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool load_txt_file(const char *file_path);
static bool load_epub_file(const char *file_path);
static void display_current_page(void);
static void get_cache_path(char *cache_path, size_t size, int page_num);
static void get_cache_meta_path(char *meta_path, size_t size, int page_num);
static void refresh_cache_window(void);
static void clear_old_cache(int window_start, int window_end);
static bool is_page_cached(int page_num);
static void next_page(void);
static void prev_page(void);
static void save_reading_progress(void);

typedef struct __attribute__((packed)) {
    uint32_t magic;             // 'CPOS'
    int32_t page_num;           // 0-based
    int32_t raw_offset_end;     // ftell() after reading this page
    int32_t logical_pos_end;    // reader->position.file_position after reading
    int32_t page_number_end;    // reader->position.page_number after reading
} page_cache_meta_t;

#define PAGE_CACHE_META_MAGIC 0x534F5043u  // 'C''P''O''S'

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
    int bytes_read = epub_parser_read_chapter(&s_reader_state.epub_reader, 0,
                                               s_reader_state.current_text,
                                               sizeof(s_reader_state.current_text));

    if (bytes_read <= 0) {
        ESP_LOGE(TAG, "Failed to read first chapter");
        epub_parser_close(&s_reader_state.epub_reader);
        return false;
    }

    s_reader_state.current_text[bytes_read] = '\0';

    s_reader_state.type = READER_TYPE_EPUB;
    s_reader_state.is_loaded = true;
    s_reader_state.current_page = 1;
    s_reader_state.total_pages = epub_parser_get_chapter_count(&s_reader_state.epub_reader);

    ESP_LOGI(TAG, "EPUB loaded: total chapters %d", s_reader_state.total_pages);

    return true;
}

/**
 * @brief 生成缓存文件路径（基于文件名+页码，避免不同文件缓存冲突）
 */
static void get_cache_path(char *cache_path, size_t size, int page_num)
{
    // 从完整路径提取文件名（不含扩展名）
    const char *filename = strrchr(s_reader_state.file_path, '/');
    if (filename) {
        filename++;  // 跳过 '/'
    } else {
        filename = s_reader_state.file_path;
    }
    
    // 复制文件名并移除扩展名
    char basename[64];
    strncpy(basename, filename, sizeof(basename) - 1);
    basename[sizeof(basename) - 1] = '\0';
    
    char *dot = strrchr(basename, '.');
    if (dot) {
        *dot = '\0';
    }
    
    // 生成缓存路径：/littlefs/cache/{文件名}_page_{页码}.txt
    snprintf(cache_path, size, "/littlefs/cache/%s_p%d.txt", basename, page_num);
}

static void get_cache_meta_path(char *meta_path, size_t size, int page_num)
{
    // 与 get_cache_path 保持同样的 basename 规则
    const char *filename = strrchr(s_reader_state.file_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = s_reader_state.file_path;
    }

    char basename[64];
    strncpy(basename, filename, sizeof(basename) - 1);
    basename[sizeof(basename) - 1] = '\0';

    char *dot = strrchr(basename, '.');
    if (dot) {
        *dot = '\0';
    }

    snprintf(meta_path, size, "/littlefs/cache/%s_p%d.meta", basename, page_num);
}

/**
 * @brief 显示当前页
 */
static void display_current_page(void)
{
    if (!s_reader_state.is_loaded) {
        return;
    }

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

    int page_info_width = display_get_text_width_font(page_info, ui_font);
    display_draw_text_font(SCREEN_WIDTH - page_info_width - 10, 5,
                           page_info, ui_font, COLOR_BLACK, COLOR_WHITE);

    // 显示文本内容
    if (s_reader_state.type == READER_TYPE_TXT) {
        int chars_read = 0;
        
        // 优化：优先从 Flash 缓存读取（比 SD 卡快得多）
        char cache_path[128];
        get_cache_path(cache_path, sizeof(cache_path), s_reader_state.current_page);
        
        FILE *cache_file = fopen(cache_path, "r");
        if (cache_file != NULL) {
            // 缓存命中：从 Flash 读取
            chars_read = fread(s_reader_state.current_text, 1, sizeof(s_reader_state.current_text) - 1, cache_file);
            fclose(cache_file);
            s_reader_state.current_text[chars_read] = '\0';
            ESP_LOGI(TAG, "Cache hit: loaded page %d from Flash (%d chars)", s_reader_state.current_page, chars_read);

            // 关键：同步 txt_reader 的文件指针/状态到“本页结束”
            // 否则下一次 cache miss 会从旧位置继续读，导致断页/重复。
            char meta_path[128];
            get_cache_meta_path(meta_path, sizeof(meta_path), s_reader_state.current_page);
            FILE *meta = fopen(meta_path, "rb");
            if (meta != NULL) {
                page_cache_meta_t m;
                size_t n = fread(&m, 1, sizeof(m), meta);
                fclose(meta);

                if (n == sizeof(m) && m.magic == PAGE_CACHE_META_MAGIC && m.page_num == s_reader_state.current_page) {
                    if (s_reader_state.txt_reader.file != NULL) {
                        fseek(s_reader_state.txt_reader.file, (long)m.raw_offset_end, SEEK_SET);
                    }
                    s_reader_state.txt_reader.position.file_position = (long)m.logical_pos_end;
                    s_reader_state.txt_reader.position.page_number = (int)m.page_number_end;
                }
            }
        } else {
            // 缓存未命中：从 SD 卡读取（首次或缓存失效）
            // 先确保 txt_reader 指向“目标页起点”，避免文件指针/页码不同步造成断页。
            txt_reader_goto_page(&s_reader_state.txt_reader,
                                 s_reader_state.current_page + 1,
                                 s_reader_state.chars_per_page);

            chars_read = txt_reader_read_page(&s_reader_state.txt_reader,
                                               s_reader_state.current_text,
                                               sizeof(s_reader_state.current_text),
                                               s_reader_state.chars_per_page);
            
            // 确保 NUL 结尾
            if (chars_read > 0) {
                size_t max_idx = sizeof(s_reader_state.current_text) - 1;
                size_t idx = (size_t)chars_read;
                if (idx > max_idx) idx = max_idx;
                s_reader_state.current_text[idx] = '\0';
            } else {
                s_reader_state.current_text[0] = '\0';
            }
            ESP_LOGI(TAG, "Cache miss: loaded page %d from SD card (%d chars)", s_reader_state.current_page, chars_read);

            // 写入缓存（内容 + meta），让后续 cache hit 可以同步 txt_reader 状态
            if (chars_read > 0) {
                FILE *new_cache = fopen(cache_path, "w");
                if (new_cache != NULL) {
                    size_t written = fwrite(s_reader_state.current_text, 1, (size_t)chars_read, new_cache);
                    fclose(new_cache);
                    if (written != (size_t)chars_read) {
                        remove(cache_path);
                    }
                }

                char meta_path[128];
                get_cache_meta_path(meta_path, sizeof(meta_path), s_reader_state.current_page);
                FILE *meta = fopen(meta_path, "wb");
                if (meta != NULL) {
                    page_cache_meta_t m = {
                        .magic = PAGE_CACHE_META_MAGIC,
                        .page_num = s_reader_state.current_page,
                        .raw_offset_end = (int32_t)ftell(s_reader_state.txt_reader.file),
                        .logical_pos_end = (int32_t)s_reader_state.txt_reader.position.file_position,
                        .page_number_end = (int32_t)s_reader_state.txt_reader.position.page_number,
                    };
                    size_t wn = fwrite(&m, 1, sizeof(m), meta);
                    fclose(meta);
                    if (wn != sizeof(m)) {
                        remove(meta_path);
                    }
                }
            }
        }

        ESP_LOGI(TAG, "txt_reader_read_page: chars_read=%d, text[0]=0x%02X text[1]=0x%02X text[2]=0x%02X text[3]=0x%02X",
                 chars_read,
                 (unsigned char)s_reader_state.current_text[0],
                 (unsigned char)s_reader_state.current_text[1],
                 (unsigned char)s_reader_state.current_text[2],
                 (unsigned char)s_reader_state.current_text[3]);

        if (chars_read > 0) {
            // UTF-8 文本换行显示 - 支持中文
            int y = 40;
            int x = 10;
            int max_width = SCREEN_WIDTH - 20;
            const char *text = s_reader_state.current_text;
            const char *p = text;
            char line[MAX_LINE_BUFFER_SIZE];
            int line_bytes = 0;

            while (*p != '\0' && y < SCREEN_HEIGHT - 40) {
                // 检查是否是换行符
                if (*p == '\n') {
                    // 渲染当前行
                    if (line_bytes > 0) {
                        line[line_bytes] = '\0';
                        display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
                    }
                    y += font_height;
                    line_bytes = 0;
                    p++;
                    continue;
                }

                // 获取下一个UTF-8字符
                uint32_t unicode;
                int char_bytes = xt_eink_font_utf8_to_utf32(p, &unicode);
                
                if (char_bytes <= 0) {
                    // 无效字符，跳过
                    p++;
                    continue;
                }

                // 优化：直接在line缓冲区中追加字符，然后检查宽度
                // 这样只需要一次复制和一次宽度计算
                memcpy(line + line_bytes, p, char_bytes);
                line[line_bytes + char_bytes] = '\0';
                
                int line_width = display_get_text_width_font(line, ui_font);
                
                if (line_width > max_width && line_bytes > 0) {
                    // 超出宽度，撤销当前字符，渲染当前行
                    line[line_bytes] = '\0';
                    display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
                    y += font_height;
                    
                    // 检查是否还有空间
                    if (y >= SCREEN_HEIGHT - 40) {
                        break;
                    }
                    
                    // 将当前字符作为新行的开头
                    memcpy(line, p, char_bytes);
                    line_bytes = char_bytes;
                    line[line_bytes] = '\0';
                } else {
                    // 字符已添加到line中，只需更新line_bytes
                    line_bytes += char_bytes;
                }

                p += char_bytes;
            }

            // 显示最后一行
            if (line_bytes > 0 && y < SCREEN_HEIGHT - 40) {
                line[line_bytes] = '\0';
                display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
            }
        }
        
        // 优化：刷新缓存窗口（预载前后多页，不阻塞当前显示）
        refresh_cache_window();
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 显示当前章节 - 支持中文
        int y = 40;
        int x = 10;
        int max_width = SCREEN_WIDTH - 20;
        const char *text = s_reader_state.current_text;
        const char *p = text;
        char line[MAX_LINE_BUFFER_SIZE];
        int line_bytes = 0;

        while (*p != '\0' && y < SCREEN_HEIGHT - 40) {
            if (*p == '\n') {
                if (line_bytes > 0) {
                    line[line_bytes] = '\0';
                    display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
                }
                y += font_height;
                line_bytes = 0;
                p++;
                continue;
            }

            uint32_t unicode;
            int char_bytes = xt_eink_font_utf8_to_utf32(p, &unicode);
            
            if (char_bytes <= 0) {
                p++;
                continue;
            }

            // 优化：直接在line缓冲区中追加字符，然后检查宽度
            memcpy(line + line_bytes, p, char_bytes);
            line[line_bytes + char_bytes] = '\0';

            int line_width = display_get_text_width_font(line, ui_font);
            
            if (line_width > max_width && line_bytes > 0) {
                // 超出宽度，撤销当前字符，渲染当前行
                line[line_bytes] = '\0';
                display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
                y += font_height;
                
                if (y >= SCREEN_HEIGHT - 40) {
                    break;
                }
                
                // 将当前字符作为新行的开头
                memcpy(line, p, char_bytes);
                line_bytes = char_bytes;
                line[line_bytes] = '\0';
            } else {
                // 字符已添加到line中，只需更新line_bytes
                line_bytes += char_bytes;
            }

            p += char_bytes;
        }

        if (line_bytes > 0 && y < SCREEN_HEIGHT - 40) {
            line[line_bytes] = '\0';
            display_draw_text_font(x, y, line, ui_font, COLOR_BLACK, COLOR_WHITE);
        }
    }

    // 显示底部提示
    display_draw_text_font(20, SCREEN_HEIGHT - 30,
                           "L/R: Page  BACK: Return",
                           ui_font, COLOR_BLACK, COLOR_WHITE);
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
        if (s_reader_state.current_page + 1 < s_reader_state.total_pages) {
            s_reader_state.current_page++;
        }
        ESP_LOGI(TAG, "Next page: %d/%d", s_reader_state.current_page + 1, s_reader_state.total_pages);
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 切换到下一章
        if (s_reader_state.current_page < s_reader_state.total_pages) {
            s_reader_state.current_page++;
            int bytes_read = epub_parser_read_chapter(&s_reader_state.epub_reader,
                                                       s_reader_state.current_page - 1,
                                                       s_reader_state.current_text,
                                                       sizeof(s_reader_state.current_text));
            if (bytes_read > 0) {
                s_reader_state.current_text[bytes_read] = '\0';
            }
        }
        ESP_LOGI(TAG, "Next page: %d/%d", s_reader_state.current_page, s_reader_state.total_pages);
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
        if (s_reader_state.current_page > 0) {
            s_reader_state.current_page--;
        }
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 切换到上一章
        if (s_reader_state.current_page > 1) {
            s_reader_state.current_page--;
            int bytes_read = epub_parser_read_chapter(&s_reader_state.epub_reader,
                                                       s_reader_state.current_page - 1,
                                                       s_reader_state.current_text,
                                                       sizeof(s_reader_state.current_text));
            if (bytes_read > 0) {
                s_reader_state.current_text[bytes_read] = '\0';
            }
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
        txt_reader_save_position(&s_reader_state.txt_reader);
        ESP_LOGI(TAG, "Reading progress saved");
    }
}

/**
 * @brief 检查页面是否已缓存
 */
static bool is_page_cached(int page_num)
{
    char cache_path[128];
    get_cache_path(cache_path, sizeof(cache_path), page_num);
    
    FILE *check = fopen(cache_path, "r");
    if (check != NULL) {
        fclose(check);
        return true;
    }
    return false;
}

/**
 * @brief 刷新缓存窗口（预载当前页前后多页）
 */
static void refresh_cache_window(void)
{
    if (!s_reader_state.is_loaded || s_reader_state.type != READER_TYPE_TXT) {
        return;
    }

    // 计算窗口范围（页码从0开始）
    int window_start = s_reader_state.current_page - CACHE_WINDOW_BEFORE;
    int window_end = s_reader_state.current_page + CACHE_WINDOW_AFTER;
    
    // 边界检查
    if (window_start < 0) window_start = 0;
    if (window_end >= s_reader_state.total_pages) window_end = s_reader_state.total_pages - 1;
    
    ESP_LOGI(TAG, "Refreshing cache window: pages %d-%d (current=%d)", 
             window_start + 1, window_end + 1, s_reader_state.current_page + 1);
    
    // 保存当前文件状态（注意：ftell 与 reader->position.file_position 可能不一致，例如 UTF-8 BOM 场景）
    long original_raw_pos = ftell(s_reader_state.txt_reader.file);
    txt_position_t original_position = s_reader_state.txt_reader.position;
    
    int cached_count = 0;
    int new_cached = 0;
    
    // 遍历窗口，预载缺失页面
    for (int page = window_start; page <= window_end; page++) {
        // 跳过已缓存页面
        if (is_page_cached(page)) {
            cached_count++;
            continue;
        }
        
        // 限制最大缓存页数
        if (new_cached >= MAX_CACHE_PAGES) {
            ESP_LOGW(TAG, "Reached max cache pages limit (%d), stopping preload", MAX_CACHE_PAGES);
            break;
        }
        
        // 跳转到目标页（txt_reader_goto_page 使用 1-based 页码；必须使用一致的 chars_per_page）
        if (!txt_reader_goto_page(&s_reader_state.txt_reader, page + 1, s_reader_state.chars_per_page)) {
            ESP_LOGW(TAG, "Failed to goto page %d", page + 1);
            continue;
        }
        
        // 读取页面内容（避免在小栈任务里放大数组）
        char *temp_buffer = (char *)malloc(4096);
        if (temp_buffer == NULL) {
            ESP_LOGE(TAG, "malloc failed for preload buffer");
            break;
        }

        int chars_read = txt_reader_read_page(&s_reader_state.txt_reader,
                                               temp_buffer,
                                               4096,
                                               s_reader_state.chars_per_page);
        
        if (chars_read > 0) {
            // 写入缓存
            char cache_path[128];
            get_cache_path(cache_path, sizeof(cache_path), page);
            
            FILE *cache_file = fopen(cache_path, "w");
            if (cache_file != NULL) {
                size_t written = fwrite(temp_buffer, 1, (size_t)chars_read, cache_file);
                fclose(cache_file);
                
                if (written == (size_t)chars_read) {
                    new_cached++;
                    ESP_LOGI(TAG, "Cached page %d (%d chars)", page + 1, chars_read);

                    // 写入 meta：记录“本页结束”的文件偏移/状态，供 cache hit 同步 txt_reader
                    char meta_path[128];
                    get_cache_meta_path(meta_path, sizeof(meta_path), page);
                    FILE *meta = fopen(meta_path, "wb");
                    if (meta != NULL) {
                        page_cache_meta_t m = {
                            .magic = PAGE_CACHE_META_MAGIC,
                            .page_num = page,
                            .raw_offset_end = (int32_t)ftell(s_reader_state.txt_reader.file),
                            .logical_pos_end = (int32_t)s_reader_state.txt_reader.position.file_position,
                            .page_number_end = (int32_t)s_reader_state.txt_reader.position.page_number,
                        };
                        size_t wn = fwrite(&m, 1, sizeof(m), meta);
                        fclose(meta);
                        if (wn != sizeof(m)) {
                            remove(meta_path);
                        }
                    }
                } else {
                    remove(cache_path);
                }
            }
        }

        free(temp_buffer);
    }
    
    // 恢复文件状态
    fseek(s_reader_state.txt_reader.file, original_raw_pos, SEEK_SET);
    s_reader_state.txt_reader.position = original_position;
    
    ESP_LOGI(TAG, "Cache window refresh complete: %d already cached, %d newly cached", 
             cached_count, new_cached);
    
    // 清理窗口外的旧缓存
    clear_old_cache(window_start, window_end);
}

/**
 * @brief 清理窗口外的旧缓存文件
 */
static void clear_old_cache(int window_start, int window_end)
{
    // 简单策略：只清理明显超出窗口的缓存（避免扫描整个目录）
    // 清理当前页前 10 页之外的缓存
    int clear_before = s_reader_state.current_page - 10;
    if (clear_before > 0 && clear_before < window_start) {
        for (int page = clear_before; page < window_start; page++) {
            char cache_path[128];
            get_cache_path(cache_path, sizeof(cache_path), page);
            
            if (remove(cache_path) == 0) {
                ESP_LOGI(TAG, "Removed old cache: page %d", page);
            }
        }
    }
    
    // 清理当前页后 10 页之外的缓存
    int clear_after = s_reader_state.current_page + 10;
    if (clear_after < s_reader_state.total_pages && clear_after > window_end) {
        for (int page = window_end + 1; page <= clear_after; page++) {
            char cache_path[128];
            get_cache_path(cache_path, sizeof(cache_path), page);
            
            if (remove(cache_path) == 0) {
                ESP_LOGI(TAG, "Removed old cache: page %d", page + 1);
            }
        }
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

    // 清理资源
    if (s_reader_state.type == READER_TYPE_TXT) {
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

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    switch (btn) {
        case BTN_RIGHT:
            // 下一页
            next_page();
            display_current_page();
            display_refresh(REFRESH_MODE_PARTIAL);
            break;

        case BTN_LEFT:
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
 * GLOBAL FUNCTIONS
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
