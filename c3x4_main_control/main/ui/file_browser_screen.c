/**
 * @file file_browser_screen.c
 * @brief 文件浏览器屏幕实现 - 手绘 UI 版本
 */

#include "file_browser_screen.h"
#include "display_engine.h"
#include "font_renderer.h"
#include "screen_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "FILE_BROWSER";

// 文件浏览器屏幕实例
static screen_t g_file_browser_screen = {0};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_show(screen_t *screen);
static void on_hide(screen_t *screen);
static void on_draw(screen_t *screen);
static void on_event(screen_t *screen, button_t btn, button_event_t event);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "File browser screen shown");
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "File browser screen hidden");
}

static void on_draw(screen_t *screen)
{
    // 清屏
    display_clear(COLOR_WHITE);

    // 绘制标题
    display_draw_text(20, 20, "File Browser", COLOR_BLACK, COLOR_WHITE);

    // 绘制提示
    display_draw_text(20, SCREEN_HEIGHT - 60,
                     "Select file to open  BACK: Return",
                     COLOR_BLACK, COLOR_WHITE);

    // TODO: 实现文件列表显示
    display_draw_text(20, 100, "File browser coming soon...", COLOR_BLACK, COLOR_WHITE);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    switch (btn) {
        case BTN_BACK:
            screen_manager_back();
            break;

        case BTN_CONFIRM:
            // TODO: 打开选中的文件
            break;

        default:
            break;
    }
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void file_browser_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing file browser screen");

    g_file_browser_screen.name = "file_browser";
    g_file_browser_screen.user_data = NULL;
    g_file_browser_screen.on_show = on_show;
    g_file_browser_screen.on_hide = on_hide;
    g_file_browser_screen.on_draw = on_draw;
    g_file_browser_screen.on_event = on_event;
    g_file_browser_screen.is_visible = false;
    g_file_browser_screen.needs_redraw = false;
}

screen_t* file_browser_screen_get_instance(void)
{
    if (g_file_browser_screen.name == NULL) {
        file_browser_screen_init();
    }
    return &g_file_browser_screen;
}
