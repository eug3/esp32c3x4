/**
 * @file font_select_screen.c
 * @brief 字体选择屏幕实现
 */

#include "font_select_screen.h"
#include "font_selector.h"
#include "xt_eink_font_impl.h"
#include "display_engine.h"
#include "ui_region_manager.h"
#include "GUI_Paint.h"
#include "screen_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "FONT_SELECT_SCREEN";
static const char *NVS_NAMESPACE = "font_settings";
static const char *NVS_KEY_FONT_PATH = "font_path";

static screen_t g_font_select_screen = {0};

// 字体列表状态
static struct {
    font_info_t fonts[FONT_SELECTOR_MAX_FONTS];
    int font_count;
    int selected_index;
    int display_offset;
} s_state = {0};

static screen_context_t *s_context = NULL;

// 字体选项：系统默认 + 扫描到的字体
typedef struct {
    char path[128];
    char name[64];
    bool is_default;
} font_option_t;

static font_option_t s_options[FONT_SELECTOR_MAX_FONTS + 1];
static int s_option_count = 0;

static void load_font_options(void)
{
    s_option_count = 0;

    // 第一个选项：系统默认（如果当前没有设置自定义字体）
    const char *current_path = xt_eink_font_get_current_path();
    strncpy(s_options[0].path, "default", sizeof(s_options[0].path) - 1);
    s_options[0].path[sizeof(s_options[0].path) - 1] = '\0';
    strncpy(s_options[0].name, "系统默认字体", sizeof(s_options[0].name) - 1);
    s_options[0].name[sizeof(s_options[0].name) - 1] = '\0';
    s_options[0].is_default = true;
    s_option_count = 1;

    // 扫描SD卡字体
    s_state.font_count = font_selector_scan_fonts(s_state.fonts, FONT_SELECTOR_MAX_FONTS);

    // 添加扫描到的字体
    for (int i = 0; i < s_state.font_count && s_option_count < FONT_SELECTOR_MAX_FONTS + 1; i++) {
        font_info_t *font = &s_state.fonts[i];
        font_option_t *opt = &s_options[s_option_count];

        strncpy(opt->path, font->path, sizeof(opt->path) - 1);
        opt->path[sizeof(opt->path) - 1] = '\0';

        // 构造显示名称：名称 + 尺寸
        if (font->width > 0 && font->height > 0) {
            snprintf(opt->name, sizeof(opt->name), "%.50s (%dx%d)",
                     font->name, font->width, font->height);
        } else {
            snprintf(opt->name, sizeof(opt->name), "%.60s", font->name);
        }

        opt->is_default = false;
        s_option_count++;
    }

    // 找到当前选中的索引
    s_state.selected_index = 0;
    if (current_path != NULL) {
        for (int i = 0; i < s_option_count; i++) {
            if (!s_options[i].is_default && strcmp(s_options[i].path, current_path) == 0) {
                s_state.selected_index = i;
                break;
            }
        }
    }

    // 计算显示偏移
    s_state.display_offset = 0;
    if (s_state.selected_index >= 4) {
        s_state.display_offset = s_state.selected_index - 4;
    }
}

static void draw_font_item(int index, bool is_selected)
{
    sFONT *font = display_get_default_ascii_font();
    int item_height = 50;
    int start_y = 80;
    int item_y = start_y + index * item_height;

    int menu_width = 400;
    int menu_x = (SCREEN_WIDTH - menu_width) / 2;

    if (is_selected) {
        display_draw_rect(menu_x - 10, item_y - 5, menu_width + 20, item_height - 10,
                         COLOR_BLACK, true);
        display_draw_text_font(menu_x, item_y + 10, s_options[index].name, font, COLOR_WHITE, COLOR_BLACK);
    } else {
        display_draw_rect(menu_x - 10, item_y - 5, menu_width + 20, item_height - 10,
                         COLOR_BLACK, false);
        display_draw_text_font(menu_x, item_y + 10, s_options[index].name, font, COLOR_BLACK, COLOR_WHITE);
    }
}

static void save_font_to_nvs(const char *path)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    if (strcmp(path, "default") == 0) {
        // 清除字体设置，使用系统默认
        err = nvs_erase_key(handle, NVS_KEY_FONT_PATH);
    } else {
        err = nvs_set_str(handle, NVS_KEY_FONT_PATH, path);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save font path: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Font path saved: %s", path);
        }
    }

    nvs_close(handle);
}

static void show_restart_dialog(void)
{
    sFONT *font = display_get_default_ascii_font();

    // 绘制确认对话框
    int dialog_w = 300;
    int dialog_h = 120;
    int dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
    int dialog_y = (SCREEN_HEIGHT - dialog_h) / 2;

    // 背景
    display_clear_region(dialog_x, dialog_y, dialog_w, dialog_h, COLOR_WHITE);
    display_draw_rect(dialog_x, dialog_y, dialog_w, dialog_h, COLOR_BLACK, true);

    // 标题
    display_draw_text_font(dialog_x + 20, dialog_y + 20, "提示", font, COLOR_WHITE, COLOR_BLACK);

    // 消息
    display_draw_text_font(dialog_x + 20, dialog_y + 50, "重启后生效", font, COLOR_WHITE, COLOR_BLACK);

    // 选项
    display_draw_text_font(dialog_x + 20, dialog_y + 85, "确认: 重启  返回: 取消", font, COLOR_WHITE, COLOR_BLACK);

    display_refresh(REFRESH_MODE_PARTIAL);
}

static void __attribute__((unused)) restart_device(void)
{
    ESP_LOGI(TAG, "Restarting device...");
    display_draw_text_font(100, 300, "正在重启...", display_get_default_ascii_font(),
                          COLOR_BLACK, COLOR_WHITE);
    display_refresh(REFRESH_MODE_FULL);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Font select screen shown");
    s_context = screen_manager_get_context();
    load_font_options();
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Font select screen hidden");
    s_context = NULL;
}

static void on_draw(screen_t *screen)
{
    if (s_context == NULL) {
        return;
    }

    sFONT *font = display_get_default_ascii_font();

    // 清屏
    display_clear(COLOR_WHITE);

    // 标题
    display_draw_text_font(20, 20, "选择字体", font, COLOR_BLACK, COLOR_WHITE);

    // 当前字体提示
    const char *current = xt_eink_font_get_current_path();
    if (current != NULL) {
        char hint[128];
        snprintf(hint, sizeof(hint), "当前: %s", strrchr(current, '/') ? strrchr(current, '/') + 1 : current);
        if (strlen(hint) > 20) {
            snprintf(hint, sizeof(hint), "当前: ...%s", strrchr(current, '/') ? strrchr(current, '/') + 1 + strlen(current) - 20 : current + strlen(current) - 20);
        }
        display_draw_text_font(20, 45, hint, font, COLOR_BLACK, COLOR_WHITE);
    } else {
        display_draw_text_font(20, 45, "当前: 系统默认", font, COLOR_BLACK, COLOR_WHITE);
    }

    // 绘制字体列表
    int visible_count = (s_option_count > 6) ? 6 : s_option_count;
    for (int i = 0; i < visible_count; i++) {
        int idx = s_state.display_offset + i;
        if (idx < s_option_count) {
            bool is_selected = (idx == s_state.selected_index);
            draw_font_item(i, is_selected);
        }
    }

    // 底部提示
    display_draw_text_font(20, SCREEN_HEIGHT - 60, "上下: 选择  确认: 确认  返回: 返回",
                           font, COLOR_BLACK, COLOR_WHITE);

    // 如果选项太多，显示滚动提示
    if (s_option_count > 6) {
        char scroll_hint[32];
        int total_pages = (s_option_count + 5) / 6;
        int current_page = s_state.display_offset / 6 + 1;
        snprintf(scroll_hint, sizeof(scroll_hint), "%d/%d", current_page, total_pages);
        int hint_width = display_get_text_width_font(scroll_hint, font);
        display_draw_text_font(SCREEN_WIDTH - hint_width - 20, SCREEN_HEIGHT - 60,
                               scroll_hint, font, COLOR_BLACK, COLOR_WHITE);
    }
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    int old_index = s_state.selected_index;
    int new_index = old_index;

    switch (btn) {
        case BTN_LEFT:
        case BTN_VOLUME_UP:
            // 向上
            if (new_index > 0) {
                new_index--;
            }
            break;

        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            // 向下
            if (new_index < s_option_count - 1) {
                new_index++;
            }
            break;

        case BTN_CONFIRM:
            // 确认选择
            {
                font_option_t *opt = &s_options[s_state.selected_index];
                ESP_LOGI(TAG, "Selected font: %s (%s)", opt->name, opt->path);

                if (opt->is_default) {
                    // 使用系统默认
                    save_font_to_nvs("default");
                } else {
                    // 使用自定义字体
                    save_font_to_nvs(opt->path);
                }

                // 显示重启对话框
                show_restart_dialog();
            }
            return;

        case BTN_BACK:
            // 返回
            screen_manager_back();
            return;

        case BTN_NONE:
        case BTN_POWER:
            // 忽略这些按钮
            break;

        default:
            // 其他按钮忽略
            break;
    }

    // 更新选中项
    if (new_index != old_index) {
        s_state.selected_index = new_index;

        // 更新显示偏移
        if (new_index < s_state.display_offset) {
            s_state.display_offset = new_index;
        } else if (new_index >= s_state.display_offset + 6) {
            s_state.display_offset = new_index - 5;
        }

        // 只刷新列表区域
        int item_height = 50;
        int start_y = 80;
        int old_item_y = start_y + (old_index - s_state.display_offset) * item_height;
        int new_item_y = start_y + (new_index - s_state.display_offset) * item_height;

        display_clear_dirty();

        // 清除并重绘旧项
        if (old_index >= s_state.display_offset && old_index < s_state.display_offset + 6) {
            display_clear_region(0, old_item_y - 5, SCREEN_WIDTH, item_height, COLOR_WHITE);
            draw_font_item(old_index - s_state.display_offset, false);
            display_mark_dirty(0, old_item_y - 5, SCREEN_WIDTH, item_height);
        }

        // 清除并重绘新项
        if (new_index >= s_state.display_offset && new_index < s_state.display_offset + 6) {
            display_clear_region(0, new_item_y - 5, SCREEN_WIDTH, item_height, COLOR_WHITE);
            draw_font_item(new_index - s_state.display_offset, true);
            display_mark_dirty(0, new_item_y - 5, SCREEN_WIDTH, item_height);
        }

        display_refresh(REFRESH_MODE_PARTIAL);
    }
}

void font_select_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing font select screen");

    g_font_select_screen.name = "font_select";
    g_font_select_screen.user_data = NULL;
    g_font_select_screen.on_show = on_show;
    g_font_select_screen.on_hide = on_hide;
    g_font_select_screen.on_draw = on_draw;
    g_font_select_screen.on_event = on_event;
    g_font_select_screen.is_visible = false;
    g_font_select_screen.needs_redraw = false;
}

screen_t* font_select_screen_get_instance(void)
{
    if (g_font_select_screen.name == NULL) {
        font_select_screen_init();
    }
    return &g_font_select_screen;
}
