/**
 * @file image_viewer_screen.c
 * @brief 图片查看器实现
 */

#include "image_viewer_screen.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "esp_log.h"

static const char *TAG = "IMAGE_VIEWER";
static screen_t g_image_viewer_screen = {0};

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Image viewer shown");
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Image viewer hidden");
}

static void on_draw(screen_t *screen)
{
    display_clear(COLOR_WHITE);
    display_draw_text(20, 20, "Image Viewer", COLOR_BLACK, COLOR_WHITE);
    display_draw_text(20, 100, "Image viewer coming soon...", COLOR_BLACK, COLOR_WHITE);
    display_draw_text(20, SCREEN_HEIGHT - 60, "BACK: Return", COLOR_BLACK, COLOR_WHITE);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event == BTN_EVENT_PRESSED && btn == BTN_BACK) {
        screen_manager_back();
    }
}

void image_viewer_screen_init(void)
{
    g_image_viewer_screen.name = "image_viewer";
    g_image_viewer_screen.on_show = on_show;
    g_image_viewer_screen.on_hide = on_hide;
    g_image_viewer_screen.on_draw = on_draw;
    g_image_viewer_screen.on_event = on_event;
}

screen_t* image_viewer_screen_get_instance(void)
{
    if (g_image_viewer_screen.name == NULL) {
        image_viewer_screen_init();
    }
    return &g_image_viewer_screen;
}
