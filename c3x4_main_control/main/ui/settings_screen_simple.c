/**
 * @file settings_screen_simple.c
 * @brief 设置屏幕实现
 */

#include "settings_screen_simple.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "fonts.h"
#include "xt_eink_font_impl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SETTINGS_SCREEN";
static screen_t g_settings_screen = {0};

// 设置菜单项
typedef enum {
    SETTING_ITEM_FONT = 0,  // 字体设置
    SETTING_ITEM_ABOUT,     // 关于
    SETTING_ITEM_COUNT
} setting_item_t;

static const char *s_setting_labels[SETTING_ITEM_COUNT] = {
    [SETTING_ITEM_FONT] = "字体设置",
    [SETTING_ITEM_ABOUT] = "关于",
};

static struct {
    setting_item_t selected_item;
    int display_offset;
} s_state = {
    .selected_item = SETTING_ITEM_FONT,
    .display_offset = 0,
};

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Settings screen shown");
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Settings screen hidden");
}

static void draw_setting_item(int index, bool is_selected)
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
        display_draw_text_font(menu_x, item_y + 12, s_setting_labels[index], font, COLOR_WHITE, COLOR_BLACK);
    } else {
        display_draw_rect(menu_x - 10, item_y - 5, menu_width + 20, item_height - 10,
                         COLOR_BLACK, false);
        display_draw_text_font(menu_x, item_y + 12, s_setting_labels[index], font, COLOR_BLACK, COLOR_WHITE);
    }
}

static void on_draw(screen_t *screen)
{
    sFONT *ui_font = display_get_default_ascii_font();

    // 清屏
    display_clear(COLOR_WHITE);

    // 标题
    display_draw_text_font(20, 20, "设置", ui_font, COLOR_BLACK, COLOR_WHITE);

    // 绘制菜单项
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
        int idx = s_state.display_offset + i;
        if (idx < SETTING_ITEM_COUNT) {
            bool is_selected = (idx == s_state.selected_item);
            draw_setting_item(i, is_selected);
        }
    }

    // 底部提示
    display_draw_text_font(20, SCREEN_HEIGHT - 60, "上下: 选择  确认: 进入  返回: 返回",
                           ui_font, COLOR_BLACK, COLOR_WHITE);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    int old_selection = s_state.selected_item;
    int new_selection = old_selection;

    switch (btn) {
        case BTN_LEFT:
        case BTN_VOLUME_UP:
            if (new_selection > 0) {
                new_selection--;
            }
            break;

        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            if (new_selection < SETTING_ITEM_COUNT - 1) {
                new_selection++;
            }
            break;

        case BTN_CONFIRM:
            switch (s_state.selected_item) {
                case SETTING_ITEM_FONT:
                    screen_manager_show_font_select();
                    break;
                case SETTING_ITEM_ABOUT:
                    // TODO: 显示关于对话框
                    break;
                default:
                    break;
            }
            return;

        case BTN_BACK:
            screen_manager_back();
            return;

        default:
            return;
    }

    // 更新选中项
    if (new_selection != old_selection) {
        s_state.selected_item = new_selection;

        // 更新显示偏移
        if (new_selection < s_state.display_offset) {
            s_state.display_offset = new_selection;
        } else if (new_selection >= s_state.display_offset + 6) {
            s_state.display_offset = new_selection - 5;
        }

        // 只刷新列表区域
        int item_height = 50;
        int start_y = 80;
        int old_item_y = start_y + (old_selection - s_state.display_offset) * item_height;
        int new_item_y = start_y + (new_selection - s_state.display_offset) * item_height;

        display_clear_dirty();

        if (old_selection >= s_state.display_offset && old_selection < s_state.display_offset + 6) {
            display_clear_region(0, old_item_y - 5, SCREEN_WIDTH, item_height, COLOR_WHITE);
            draw_setting_item(old_selection - s_state.display_offset, false);
            display_mark_dirty(0, old_item_y - 5, SCREEN_WIDTH, item_height);
        }

        if (new_selection >= s_state.display_offset && new_selection < s_state.display_offset + 6) {
            display_clear_region(0, new_item_y - 5, SCREEN_WIDTH, item_height, COLOR_WHITE);
            draw_setting_item(new_selection - s_state.display_offset, true);
            display_mark_dirty(0, new_item_y - 5, SCREEN_WIDTH, item_height);
        }

        display_refresh(REFRESH_MODE_PARTIAL);
    }
}

void settings_screen_simple_init(void)
{
    ESP_LOGI(TAG, "Initializing settings screen");

    g_settings_screen.name = "settings";
    g_settings_screen.on_show = on_show;
    g_settings_screen.on_hide = on_hide;
    g_settings_screen.on_draw = on_draw;
    g_settings_screen.on_event = on_event;
    g_settings_screen.is_visible = false;
    g_settings_screen.needs_redraw = false;
}

screen_t* settings_screen_simple_get_instance(void)
{
    if (g_settings_screen.name == NULL) {
        settings_screen_simple_init();
    }
    return &g_settings_screen;
}
