/**
 * @file settings_screen_simple.c
 * @brief 设置屏幕实现
 */

#include "settings_screen_simple.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "fonts.h"
#include "esp_log.h"

static const char *TAG = "SETTINGS_SCREEN";
static screen_t g_settings_screen = {0};

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
    sFONT *ui_font = display_get_default_ascii_font();

    display_clear(COLOR_WHITE);
    display_draw_text_font(20, 20, "设置", ui_font, COLOR_BLACK, COLOR_WHITE);
    display_draw_text_font(20, 100, "设置功能开发中...", ui_font, COLOR_BLACK, COLOR_WHITE);
    display_draw_text_font(20, SCREEN_HEIGHT - 60, "返回: 返回", ui_font, COLOR_BLACK, COLOR_WHITE);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event == BTN_EVENT_PRESSED && btn == BTN_BACK) {
        screen_manager_back();
    }
}

void settings_screen_simple_init(void)
{
    g_settings_screen.name = "settings";
    g_settings_screen.on_show = on_show;
    g_settings_screen.on_hide = on_hide;
    g_settings_screen.on_draw = on_draw;
    g_settings_screen.on_event = on_event;
}

screen_t* settings_screen_simple_get_instance(void)
{
    if (g_settings_screen.name == NULL) {
        settings_screen_simple_init();
    }
    return &g_settings_screen;
}
