/**
 * @file reader_screen.c
 * @brief 阅读器屏幕实现 - 支持 TXT 和 EPUB 格式的电子书阅读
 */

#include "reader_screen.h"
#include "txt_reader.h"
#include "epub_parser.h"
#include "font_manager.h"
#include "lvgl_driver.h"
#include "screen_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>

static const char *TAG = "READER_SCREEN";

// LVGL 内置字体 - 只使用可用的字体
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_montserrat_20;
extern const lv_font_t lv_font_montserrat_24;

// 每页估算字符数（根据屏幕尺寸和字体大小）
#define CHARS_PER_PAGE_SMALL   1500   // 小字体
#define CHARS_PER_PAGE_MEDIUM  1000   // 中字体
#define CHARS_PER_PAGE_LARGE   600    // 大字体

// 默认缓冲区大小
#define TEXT_BUFFER_SIZE 8192

// 阅读器屏幕状态定义（与头文件的前置声明匹配）
struct reader_state_t {
    char file_path[256];
    book_type_t book_type;
    bool is_open;

    // 阅读进度
    int current_page;
    int total_pages;

    // TXT/EPUB 数据
    txt_reader_t *txt_reader;
    epub_reader_t *epub_reader;

    // 缓冲区
    char *text_buffer;
    size_t buffer_size;

    // 设置
    reader_settings_t settings;

    // UI 组件
    lv_obj_t *screen;
    lv_obj_t *text_label;
    lv_obj_t *progress_label;
    lv_obj_t *status_bar;
    lv_obj_t *menu;

    lv_indev_t *indev;
    lv_group_t *group;

    // 待处理动作
    enum {
        READER_ACTION_NONE = 0,
        READER_ACTION_NEXT_PAGE,
        READER_ACTION_PREV_PAGE,
        READER_ACTION_SHOW_MENU,
        READER_ACTION_HIDE_MENU,
        READER_ACTION_EXIT,
    } pending_action;
};

static struct reader_state_t g_reader_state = {0};

// 前置声明
static void update_page_display(void);
static int get_chars_per_page(int font_size);
static void reader_process_pending_action_cb(void *user_data);
static void reader_screen_destroy_cb(lv_event_t *e);

// 根据字体大小获取每页字符数
static int get_chars_per_page(int font_size) {
    if (font_size <= 12) {
        return CHARS_PER_PAGE_LARGE;
    } else if (font_size <= 16) {
        return CHARS_PER_PAGE_MEDIUM;
    } else {
        return CHARS_PER_PAGE_SMALL;
    }
}

// 根据文件扩展名获取书籍类型
static book_type_t get_book_type(const char *file_path) {
    if (file_path == NULL) {
        return BOOK_TYPE_NONE;
    }

    const char *ext = strrchr(file_path, '.');
    if (ext == NULL) {
        return BOOK_TYPE_NONE;
    }

    if (strcasecmp(ext, ".txt") == 0) {
        return BOOK_TYPE_TXT;
    } else if (strcasecmp(ext, ".epub") == 0) {
        return BOOK_TYPE_EPUB;
    }

    return BOOK_TYPE_NONE;
}

// 根据字体大小获取字体指针
static const lv_font_t* get_lvgl_font(int font_size) {
    switch (font_size) {
        case 14: return &lv_font_montserrat_14;
        case 16: return &lv_font_montserrat_16;
        case 20: return &lv_font_montserrat_20;
        case 24: return &lv_font_montserrat_24;
        default: return &lv_font_montserrat_14;
    }
}

// 更新页面显示
static void update_page_display(void) {
    if (!g_reader_state.is_open || g_reader_state.text_buffer == NULL) {
        return;
    }

    // 清空缓冲区
    memset(g_reader_state.text_buffer, 0, g_reader_state.buffer_size);

    int chars_read = 0;

    if (g_reader_state.book_type == BOOK_TYPE_TXT && g_reader_state.txt_reader != NULL) {
        int chars_per_page = get_chars_per_page(g_reader_state.settings.font_size);
        chars_read = txt_reader_read_page(g_reader_state.txt_reader, g_reader_state.text_buffer,
                                          g_reader_state.buffer_size, chars_per_page);

        // 更新位置
        txt_position_t pos = txt_reader_get_position(g_reader_state.txt_reader);
        g_reader_state.current_page = pos.page_number;
        g_reader_state.total_pages = txt_reader_get_total_pages(g_reader_state.txt_reader, chars_per_page);

    } else if (g_reader_state.book_type == BOOK_TYPE_EPUB && g_reader_state.epub_reader != NULL) {
        // EPUB 支持（简化版本）
        snprintf(g_reader_state.text_buffer, g_reader_state.buffer_size,
                "EPUB support\n\nFile: %s\n\nEPUB format requires pre-extraction.\nPlease extract EPUB to /sdcard/XTCache/ directory.",
                g_reader_state.file_path);
        chars_read = strlen(g_reader_state.text_buffer);
    }

    // 更新显示
    if (chars_read > 0 && g_reader_state.text_label != NULL) {
        lv_label_set_text(g_reader_state.text_label, g_reader_state.text_buffer);
    }

    // 更新进度
    if (g_reader_state.progress_label != NULL) {
        char progress_str[32];
        snprintf(progress_str, sizeof(progress_str), "%d / %d",
                 g_reader_state.current_page, g_reader_state.total_pages);
        lv_label_set_text(g_reader_state.progress_label, progress_str);
    }
}

// 清理阅读器资源
static void cleanup_reader(void) {
    // 保存进度
    if (g_reader_state.txt_reader != NULL) {
        txt_reader_save_position(g_reader_state.txt_reader);
        txt_reader_close(g_reader_state.txt_reader);
        txt_reader_cleanup(g_reader_state.txt_reader);
        free(g_reader_state.txt_reader);
        g_reader_state.txt_reader = NULL;
    }

    if (g_reader_state.epub_reader != NULL) {
        epub_parser_save_position(g_reader_state.epub_reader);
        epub_parser_close(g_reader_state.epub_reader);
        epub_parser_cleanup(g_reader_state.epub_reader);
        free(g_reader_state.epub_reader);
        g_reader_state.epub_reader = NULL;
    }

    if (g_reader_state.text_buffer != NULL) {
        free(g_reader_state.text_buffer);
        g_reader_state.text_buffer = NULL;
    }

    g_reader_state.is_open = false;
}

// 事件回调
static void reader_key_event_cb(lv_event_t *e) {
    lv_key_t key = lv_indev_get_key(lv_indev_get_act());

    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_RIGHT:
            // 下一页
            g_reader_state.pending_action = READER_ACTION_NEXT_PAGE;
            lv_async_call(reader_process_pending_action_cb, NULL);
            break;

        case LV_KEY_DOWN:
        case LV_KEY_LEFT:
            // 上一页
            g_reader_state.pending_action = READER_ACTION_PREV_PAGE;
            lv_async_call(reader_process_pending_action_cb, NULL);
            break;

        case LV_KEY_ENTER:
            // 显示/隐藏菜单
            if (lv_obj_has_flag(g_reader_state.menu, LV_OBJ_FLAG_HIDDEN)) {
                g_reader_state.pending_action = READER_ACTION_SHOW_MENU;
            } else {
                g_reader_state.pending_action = READER_ACTION_HIDE_MENU;
            }
            lv_async_call(reader_process_pending_action_cb, NULL);
            break;

        case LV_KEY_ESC:
            // 退出阅读器
            g_reader_state.pending_action = READER_ACTION_EXIT;
            lv_async_call(reader_process_pending_action_cb, NULL);
            break;

        default:
            break;
    }
}

// 处理待处理动作
static void reader_process_pending_action_cb(void *user_data) {
    (void)user_data;

    switch (g_reader_state.pending_action) {
        case READER_ACTION_NEXT_PAGE:
            update_page_display();
            lvgl_trigger_render(NULL);
            lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
            lvgl_display_refresh_partial();
            break;

        case READER_ACTION_PREV_PAGE:
            // TXT 阅读器可以后退
            if (g_reader_state.book_type == BOOK_TYPE_TXT && g_reader_state.txt_reader != NULL) {
                txt_position_t pos = txt_reader_get_position(g_reader_state.txt_reader);
                if (pos.page_number > 1) {
                    txt_reader_goto_page(g_reader_state.txt_reader, pos.page_number - 1);
                    update_page_display();
                    lvgl_trigger_render(NULL);
                    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
                    lvgl_display_refresh_partial();
                }
            }
            break;

        case READER_ACTION_SHOW_MENU:
            lv_obj_clear_flag(g_reader_state.menu, LV_OBJ_FLAG_HIDDEN);
            lvgl_trigger_render(NULL);
            lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
            lvgl_display_refresh_partial();
            break;

        case READER_ACTION_HIDE_MENU:
            lv_obj_add_flag(g_reader_state.menu, LV_OBJ_FLAG_HIDDEN);
            lvgl_trigger_render(NULL);
            lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
            lvgl_display_refresh_partial();
            break;

        case READER_ACTION_EXIT:
            // 返回文件浏览器
            screen_manager_go_back();
            break;

        default:
            break;
    }

    g_reader_state.pending_action = READER_ACTION_NONE;
}

// 屏幕销毁回调
static void reader_screen_destroy_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Reader screen destroy callback");
    cleanup_reader();
    memset(&g_reader_state, 0, sizeof(g_reader_state));
}

// 创建阅读器屏幕的 wrapper 函数
void reader_screen_create_wrapper(const char *file_path, lv_indev_t *indev) {
    if (file_path == NULL || indev == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for reader screen");
        return;
    }

    ESP_LOGI(TAG, "Creating reader screen for: %s", file_path);

    // 检测文件类型
    book_type_t book_type = get_book_type(file_path);
    if (book_type == BOOK_TYPE_NONE) {
        ESP_LOGE(TAG, "Unsupported file type: %s", file_path);
        return;
    }

    // 清理旧状态
    if (g_reader_state.is_open) {
        cleanup_reader();
    }

    // 初始化状态
    memset(&g_reader_state, 0, sizeof(g_reader_state));
    strncpy(g_reader_state.file_path, file_path, sizeof(g_reader_state.file_path) - 1);
    g_reader_state.book_type = book_type;
    g_reader_state.settings.font_size = 14;
    g_reader_state.settings.line_spacing = 2;
    g_reader_state.settings.margin = 10;
    g_reader_state.settings.auto_refresh = true;
    g_reader_state.indev = indev;

    // 分配文本缓冲区
    g_reader_state.buffer_size = TEXT_BUFFER_SIZE;
    g_reader_state.text_buffer = malloc(TEXT_BUFFER_SIZE);
    if (g_reader_state.text_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate text buffer");
        return;
    }

    // 打开文件
    if (book_type == BOOK_TYPE_TXT) {
        g_reader_state.txt_reader = calloc(1, sizeof(txt_reader_t));
        if (g_reader_state.txt_reader != NULL) {
            if (txt_reader_init(g_reader_state.txt_reader) &&
                txt_reader_open(g_reader_state.txt_reader, file_path, TXT_ENCODING_AUTO)) {
                g_reader_state.is_open = true;
                // 尝试加载上次阅读位置
                txt_reader_load_position(g_reader_state.txt_reader);
            } else {
                txt_reader_cleanup(g_reader_state.txt_reader);
                free(g_reader_state.txt_reader);
                g_reader_state.txt_reader = NULL;
            }
        }
    } else if (book_type == BOOK_TYPE_EPUB) {
        g_reader_state.epub_reader = calloc(1, sizeof(epub_reader_t));
        if (g_reader_state.epub_reader != NULL) {
            if (epub_parser_init(g_reader_state.epub_reader) &&
                epub_parser_open(g_reader_state.epub_reader, file_path)) {
                g_reader_state.is_open = true;
                // 尝试加载上次阅读位置
                epub_parser_load_position(g_reader_state.epub_reader);
            } else {
                epub_parser_cleanup(g_reader_state.epub_reader);
                free(g_reader_state.epub_reader);
                g_reader_state.epub_reader = NULL;
            }
        }
    }

    if (!g_reader_state.is_open) {
        ESP_LOGE(TAG, "Failed to open book: %s", file_path);
        cleanup_reader();
        return;
    }

    // 创建屏幕容器
    g_reader_state.screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_reader_state.screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(g_reader_state.screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(g_reader_state.screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_reader_state.screen, 0, 0);
    lv_obj_set_style_pad_all(g_reader_state.screen, 0, 0);

    // 添加销毁回调
    lv_obj_add_event_cb(g_reader_state.screen, reader_screen_destroy_cb, LV_EVENT_DELETE, NULL);

    // 创建状态栏
    g_reader_state.status_bar = lv_obj_create(g_reader_state.screen);
    lv_obj_set_size(g_reader_state.status_bar, LV_PCT(100), 40);
    lv_obj_set_pos(g_reader_state.status_bar, 0, 0);
    lv_obj_set_style_pad_all(g_reader_state.status_bar, 8, 0);
    lv_obj_set_style_bg_color(g_reader_state.status_bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(g_reader_state.status_bar, 0, 0);

    // 进度标签
    g_reader_state.progress_label = lv_label_create(g_reader_state.status_bar);
    lv_obj_set_style_text_font(g_reader_state.progress_label, get_lvgl_font(14), 0);
    lv_obj_set_style_text_color(g_reader_state.progress_label, lv_color_white(), 0);
    lv_obj_align(g_reader_state.progress_label, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_label_set_text(g_reader_state.progress_label, "0 / 0");

    // 标题标签
    lv_obj_t *title_label = lv_label_create(g_reader_state.status_bar);
    lv_obj_set_style_text_font(title_label, get_lvgl_font(14), 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_width(title_label, LV_PCT(80));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 5, 0);

    const char *filename = strrchr(file_path, '/');
    const char *display_name = filename ? filename + 1 : file_path;
    lv_label_set_text(title_label, display_name);

    // 创建阅读区域
    lv_obj_t *reading_area = lv_obj_create(g_reader_state.screen);
    lv_obj_set_size(reading_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(reading_area, 0, 40);
    lv_obj_set_style_pad_all(reading_area, 10, 0);
    lv_obj_set_style_bg_color(reading_area, lv_color_white(), 0);
    lv_obj_set_style_border_width(reading_area, 0, 0);

    // 文本标签 - 使用当前设置的字体或默认字体
    const lv_font_t *current_font = font_manager_get_font();
    if (current_font == NULL) {
        current_font = get_lvgl_font(14);
    }

    g_reader_state.text_label = lv_label_create(reading_area);
    lv_obj_set_width(g_reader_state.text_label, LV_PCT(100));
    lv_obj_set_style_text_font(g_reader_state.text_label, current_font, 0);
    lv_obj_set_style_text_color(g_reader_state.text_label, lv_color_black(), 0);
    lv_label_set_long_mode(g_reader_state.text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_reader_state.text_label, LV_TEXT_ALIGN_LEFT, 0);

    // 创建菜单
    g_reader_state.menu = lv_obj_create(g_reader_state.screen);
    lv_obj_set_size(g_reader_state.menu, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(g_reader_state.menu, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(g_reader_state.menu, 10, 0);
    lv_obj_set_style_bg_color(g_reader_state.menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_reader_state.menu, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_reader_state.menu, 0, 0);
    lv_obj_add_flag(g_reader_state.menu, LV_OBJ_FLAG_HIDDEN);

    // 添加菜单说明
    lv_obj_t *menu_label = lv_label_create(g_reader_state.menu);
    lv_obj_set_style_text_font(menu_label, get_lvgl_font(14), 0);
    lv_obj_set_style_text_color(menu_label, lv_color_white(), 0);
    lv_label_set_text(menu_label, "菜单:\n↑/→: 下一页\n↓/←: 上一页\nEnter: 返回\nESC: 退出");

    // 设置输入设备
    lv_indev_set_group(indev, NULL);
    g_reader_state.group = lv_group_create();
    lv_group_add_obj(g_reader_state.group, g_reader_state.screen);
    lv_indev_set_group(indev, g_reader_state.group);
    lv_group_set_editing(g_reader_state.group, false);

    // 添加按键事件回调
    lv_obj_add_event_cb(g_reader_state.screen, reader_key_event_cb, LV_EVENT_KEY, NULL);

    // 读取第一页
    update_page_display();

    // 触发渲染
    lvgl_trigger_render(NULL);
    lvgl_display_refresh_full();

    ESP_LOGI(TAG, "Reader screen created successfully");
}

// 以下函数提供给外部调用（如果需要直接操作）

reader_state_t* reader_screen_get_state(void) {
    return (reader_state_t*)&g_reader_state;
}

void reader_screen_next_page(void) {
    if (g_reader_state.is_open) {
        update_page_display();
        lvgl_trigger_render(NULL);
        lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
        lvgl_display_refresh_partial();
    }
}

void reader_screen_prev_page(void) {
    if (g_reader_state.is_open && g_reader_state.book_type == BOOK_TYPE_TXT && g_reader_state.txt_reader != NULL) {
        txt_position_t pos = txt_reader_get_position(g_reader_state.txt_reader);
        if (pos.page_number > 1) {
            txt_reader_goto_page(g_reader_state.txt_reader, pos.page_number - 1);
            update_page_display();
            lvgl_trigger_render(NULL);
            lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
            lvgl_display_refresh_partial();
        }
    }
}

bool reader_screen_save_progress(void) {
    if (!g_reader_state.is_open) {
        return false;
    }

    bool saved = false;

    if (g_reader_state.book_type == BOOK_TYPE_TXT && g_reader_state.txt_reader != NULL) {
        saved = txt_reader_save_position(g_reader_state.txt_reader);
    } else if (g_reader_state.book_type == BOOK_TYPE_EPUB && g_reader_state.epub_reader != NULL) {
        saved = epub_parser_save_position(g_reader_state.epub_reader);
    }

    if (saved) {
        ESP_LOGI(TAG, "Reading progress saved");
    }

    return saved;
}

void reader_screen_set_font_size(int font_size) {
    g_reader_state.settings.font_size = font_size;

    if (g_reader_state.text_label != NULL) {
        const lv_font_t *font = get_lvgl_font(font_size);
        lv_obj_set_style_text_font(g_reader_state.text_label, font, 0);
        update_page_display();
        lvgl_trigger_render(NULL);
        lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
        lvgl_display_refresh_partial();
    }
}
