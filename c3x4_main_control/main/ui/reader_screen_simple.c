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
    .chars_per_page = 2000,  // 每页约2000字符
    .is_loaded = false,
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool load_txt_file(const char *file_path);
static bool load_epub_file(const char *file_path);
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
 * @brief 显示当前页
 */
static void display_current_page(void)
{
    if (!s_reader_state.is_loaded) {
        return;
    }

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
                 s_reader_state.current_page, s_reader_state.total_pages);
    } else {
        snprintf(page_info, sizeof(page_info), "Chapter %d/%d",
                 s_reader_state.current_page, s_reader_state.total_pages);
    }

    int page_info_width = display_get_text_width_font(page_info, ui_font);
    display_draw_text_font(SCREEN_WIDTH - page_info_width - 10, 5,
                           page_info, ui_font, COLOR_BLACK, COLOR_WHITE);

    // 显示文本内容
    if (s_reader_state.type == READER_TYPE_TXT) {
        // TXT: 读取当前页
        int chars_read = txt_reader_read_page(&s_reader_state.txt_reader,
                                               s_reader_state.current_text,
                                               sizeof(s_reader_state.current_text),
                                               s_reader_state.chars_per_page);

        if (chars_read > 0) {
            // UTF-8 文本换行显示 - 支持中文
            int y = 40;
            int x = 10;
            int max_width = SCREEN_WIDTH - 20;
            uint8_t *framebuffer = display_get_framebuffer();
            const char *text = s_reader_state.current_text;
            const char *p = text;
            char line[512];
            int line_bytes = 0;

            while (*p != '\0' && y < SCREEN_HEIGHT - 40) {
                // 检查是否是换行符
                if (*p == '\n') {
                    // 渲染当前行
                    if (line_bytes > 0) {
                        line[line_bytes] = '\0';
                        xt_eink_font_render_text(x, y, line, COLOR_BLACK, 
                                                framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
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

                // 临时添加字符到行，检查是否超出宽度
                char temp_line[512];
                memcpy(temp_line, line, line_bytes);
                memcpy(temp_line + line_bytes, p, char_bytes);
                temp_line[line_bytes + char_bytes] = '\0';

                int line_width = xt_eink_font_get_text_width(temp_line);
                
                if (line_width > max_width && line_bytes > 0) {
                    // 当前行已满，先渲染当前行
                    line[line_bytes] = '\0';
                    xt_eink_font_render_text(x, y, line, COLOR_BLACK,
                                            framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
                    y += font_height;
                    line_bytes = 0;
                    
                    // 检查是否还有空间
                    if (y >= SCREEN_HEIGHT - 40) {
                        break;
                    }
                    
                    // 将当前字符加入新行
                    memcpy(line, p, char_bytes);
                    line_bytes = char_bytes;
                } else {
                    // 添加字符到当前行
                    memcpy(line + line_bytes, p, char_bytes);
                    line_bytes += char_bytes;
                }

                p += char_bytes;
            }

            // 显示最后一行
            if (line_bytes > 0 && y < SCREEN_HEIGHT - 40) {
                line[line_bytes] = '\0';
                xt_eink_font_render_text(x, y, line, COLOR_BLACK,
                                        framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
            }
        }
    } else if (s_reader_state.type == READER_TYPE_EPUB) {
        // EPUB: 显示当前章节 - 支持中文
        int y = 40;
        int x = 10;
        int max_width = SCREEN_WIDTH - 20;
        uint8_t *framebuffer = display_get_framebuffer();
        const char *text = s_reader_state.current_text;
        const char *p = text;
        char line[512];
        int line_bytes = 0;

        while (*p != '\0' && y < SCREEN_HEIGHT - 40) {
            if (*p == '\n') {
                if (line_bytes > 0) {
                    line[line_bytes] = '\0';
                    xt_eink_font_render_text(x, y, line, COLOR_BLACK,
                                            framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
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

            char temp_line[512];
            memcpy(temp_line, line, line_bytes);
            memcpy(temp_line + line_bytes, p, char_bytes);
            temp_line[line_bytes + char_bytes] = '\0';

            int line_width = xt_eink_font_get_text_width(temp_line);
            
            if (line_width > max_width && line_bytes > 0) {
                line[line_bytes] = '\0';
                xt_eink_font_render_text(x, y, line, COLOR_BLACK,
                                        framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
                y += font_height;
                line_bytes = 0;
                
                if (y >= SCREEN_HEIGHT - 40) {
                    break;
                }
                
                memcpy(line, p, char_bytes);
                line_bytes = char_bytes;
            } else {
                memcpy(line + line_bytes, p, char_bytes);
                line_bytes += char_bytes;
            }

            p += char_bytes;
        }

        if (line_bytes > 0 && y < SCREEN_HEIGHT - 40) {
            line[line_bytes] = '\0';
            xt_eink_font_render_text(x, y, line, COLOR_BLACK,
                                    framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
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
        s_reader_state.current_page++;
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
    }

    ESP_LOGI(TAG, "Next page: %d/%d", s_reader_state.current_page, s_reader_state.total_pages);
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
        if (s_reader_state.current_page > 1) {
            txt_reader_goto_page(&s_reader_state.txt_reader, s_reader_state.current_page - 1);
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
