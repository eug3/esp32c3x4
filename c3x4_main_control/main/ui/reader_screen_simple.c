/**
 * @file reader_screen_simple.c
 * @brief 阅读器屏幕实现 - 手绘 UI 版本
 */

#include "reader_screen_simple.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "fonts.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "READER_SCREEN";

static screen_t g_reader_screen = {0};

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Reader screen shown, file=%s", (char*)screen->user_data);
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Reader screen hidden");
}

static void on_draw(screen_t *screen)
{
    sFONT *body_font = &SourceSansPro16;

    display_clear(COLOR_WHITE);
    display_draw_text_font(20, 20, "Reader", body_font, COLOR_BLACK, COLOR_WHITE);

    if (screen->user_data != NULL) {
        char info[128];
        snprintf(info, sizeof(info), "File: %s", (char*)screen->user_data);
        display_draw_text_font(20, 100, info, body_font, COLOR_BLACK, COLOR_WHITE);
    }

    display_draw_text_font(20, SCREEN_HEIGHT - 60,
                           "UP/DOWN: Page  BACK: Return",
                           body_font, COLOR_BLACK, COLOR_WHITE);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) return;

    if (btn == BTN_BACK) {
        screen_manager_back();
    }
}

void reader_screen_init(void)
{
    g_reader_screen.name = "reader";
    g_reader_screen.on_show = on_show;
    g_reader_screen.on_hide = on_hide;
    g_reader_screen.on_draw = on_draw;
    g_reader_screen.on_event = on_event;
}

screen_t* reader_screen_get_instance(void)
{
    if (g_reader_screen.name == NULL) {
        reader_screen_init();
    }
    return &g_reader_screen;
}
