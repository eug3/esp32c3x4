/**
 * @file screen_manager.c
 * @brief 屏幕导航管理器实现
 */

#include "screen_manager.h"
#include "../lvgl_driver.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SCREEN_MGR";

static screen_context_t *g_context = NULL;

void screen_manager_init(screen_context_t *ctx)
{
    g_context = ctx;
    ESP_LOGI(TAG, "Screen manager initialized");
}

void screen_manager_show_index(void)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Navigating to index screen");

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();
    
    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    extern void index_screen_create(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str, lv_indev_t *indev);

    index_screen_create(
        g_context->battery_mv,
        g_context->battery_pct,
        g_context->charging,
        g_context->version_str,
        g_context->indev
    );
}

void screen_manager_show_file_browser(void)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Navigating to file browser screen");

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();
    
    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    extern void file_browser_screen_create(lv_indev_t *indev);

    file_browser_screen_create(g_context->indev);
}

void screen_manager_show_settings(void)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Navigating to settings screen");

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();
    
    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    extern void settings_screen_create(lv_indev_t *indev);

    settings_screen_create(g_context->indev);
}

screen_context_t* screen_manager_get_context(void)
{
    return g_context;
}
