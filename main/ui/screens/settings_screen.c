/**
 * @file settings_screen.c
 * @brief 设置屏幕实现 - 使用 paginated_menu 组件
 */

#include "settings_screen.h"
#include "display_engine.h"
#include "paginated_menu.h"
#include "screen_manager.h"
#include "fonts.h"
#include "xt_eink_font_impl.h"
#include "wallpaper_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SETTINGS_SCREEN";
static screen_t g_settings_screen = {0};
static paginated_menu_t s_menu = {0};

// 设置菜单项
typedef enum {
    SETTING_ITEM_WALLPAPER = 0,
    SETTING_ITEM_FONT,
    SETTING_ITEM_ABOUT,
    SETTING_ITEM_COUNT
} setting_item_t;

static const char *s_setting_labels[SETTING_ITEM_COUNT] = {
    [SETTING_ITEM_WALLPAPER] = "壁纸管理",
    [SETTING_ITEM_FONT] = "字体设置",
    [SETTING_ITEM_ABOUT] = "关于",
};

/**********************
 * PRIVATE FUNCTIONS
 **********************/

/**
 * @brief 菜单项获取回调
 */
static bool settings_menu_item_getter(int index, char *out_text, int out_text_size, bool *out_is_selected)
{
    if (index < 0 || index >= SETTING_ITEM_COUNT) {
        return false;
    }

    strncpy(out_text, s_setting_labels[index], out_text_size - 1);
    out_text[out_text_size - 1] = '\0';

    int selected_index = paginated_menu_get_selected_index(&s_menu);
    *out_is_selected = (index == selected_index);

    return true;
}

/**********************
 * SCREEN CALLBACKS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Settings screen shown");
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Settings screen hidden");
}

static void on_draw(screen_t *screen)
{
    display_clear(COLOR_WHITE);

    // 标题
    display_draw_text_menu(20, 20, "设置", COLOR_BLACK, COLOR_WHITE);

    // 绘制菜单
    paginated_menu_draw(&s_menu);

    // 底部提示
    paginated_menu_draw_footer_hint(&s_menu, "上下: 选择  确认: 进入  返回: 返回", 20, SCREEN_HEIGHT - 60);

    display_refresh(REFRESH_MODE_PARTIAL);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    // 处理导航按钮
    // 对于只有单页的菜单：LEFT/RIGHT 和 VOLUME_UP/DOWN 都用于上下导航
    // 对于多页菜单：LEFT/RIGHT 用于翻页，VOLUME_UP/DOWN 用于页内导航
    if (btn == BTN_LEFT || btn == BTN_RIGHT ||
        btn == BTN_VOLUME_UP || btn == BTN_VOLUME_DOWN) {

        // 先尝试普通的菜单处理
        bool changed = paginated_menu_handle_button(&s_menu, btn, NULL, NULL);
        
        // 如果没有改变且是翻页按钮，尝试在单页菜单中进行上下导航
        if (!changed && (btn == BTN_LEFT || btn == BTN_RIGHT)) {
            // 对于单页菜单，LEFT/RIGHT 也应该作为上下导航使用
            int delta = (btn == BTN_LEFT) ? -1 : 1;
            changed = paginated_menu_move_selection(&s_menu, delta);
        }
        
        if (changed) {
            // 状态改变，重绘整屏
            screen->needs_redraw = true;
        }
        return;  // 导航按钮已处理
    }


    // 处理功能按钮
    switch (btn) {
        case BTN_CONFIRM: {
            int selected = paginated_menu_get_selected_index(&s_menu);
            switch (selected) {
                case SETTING_ITEM_WALLPAPER:
                    // 进入壁纸管理屏幕（列表 + 预览 + 选择）
                    screen_manager_show("wallpaper");
                    break;
                case SETTING_ITEM_FONT:
                    screen_manager_show_font_select();
                    break;
                case SETTING_ITEM_ABOUT:
                    // TODO: 显示关于对话框
                    break;
                default:
                    break;
            }
            break;
        }

        case BTN_BACK:
            screen_manager_back();
            break;

        default:
            break;
    }
}

/**********************
 * PUBLIC API
 **********************/

void settings_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing settings screen");

    // 初始化菜单
    paginated_menu_config_t config = {
        .start_y = 80,
        .item_height = 50,
        .bottom_margin = 80,
        .menu_width = 400,
        .text_offset_y = 12,
        .items_per_page = 10,
        .item_getter = settings_menu_item_getter,
        .item_drawer = NULL,  // 使用默认绘制器
        .user_data = NULL,
        .padding_x = 10,
        .padding_y = 5,
        .show_page_hint = true,
        .page_hint_x = -1,
        .page_hint_y = -1
    };

    if (!paginated_menu_init(&s_menu, &config)) {
        ESP_LOGE(TAG, "Failed to initialize menu");
        return;
    }

    paginated_menu_set_total_count(&s_menu, SETTING_ITEM_COUNT);
    paginated_menu_set_selected_index(&s_menu, SETTING_ITEM_FONT);

    g_settings_screen.name = "settings";
    g_settings_screen.on_show = on_show;
    g_settings_screen.on_hide = on_hide;
    g_settings_screen.on_draw = on_draw;
    g_settings_screen.on_event = on_event;
    g_settings_screen.is_visible = false;
    g_settings_screen.needs_redraw = false;
}

screen_t* settings_screen_get_instance(void)
{
    if (g_settings_screen.name == NULL) {
        settings_screen_init();
    }
    return &g_settings_screen;
}
