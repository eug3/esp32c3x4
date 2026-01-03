/**
 * @file screen_manager.c
 * @brief 屏幕导航管理器实现
 */

#include "screen_manager.h"
#include "../lvgl_driver.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SCREEN_MGR";

// 导航历史栈 - 最大深度 10
#define NAVIGATION_STACK_MAX_DEPTH 10

static screen_context_t *g_context = NULL;
static screen_type_t g_navigation_stack[NAVIGATION_STACK_MAX_DEPTH] = {SCREEN_TYPE_INDEX};
static int g_navigation_stack_top = 0;  // 栈顶指针（指向当前屏幕）
static screen_type_t g_current_screen = SCREEN_TYPE_INDEX;

void screen_manager_init(screen_context_t *ctx)
{
    g_context = ctx;
    g_navigation_stack[0] = SCREEN_TYPE_INDEX;
    g_navigation_stack_top = 0;
    g_current_screen = SCREEN_TYPE_INDEX;
    ESP_LOGI(TAG, "Screen manager initialized");
}

// 内部函数：将屏幕压入导航栈
static void push_screen(screen_type_t screen_type)
{
    if (g_navigation_stack_top < NAVIGATION_STACK_MAX_DEPTH - 1) {
        g_navigation_stack_top++;
        g_navigation_stack[g_navigation_stack_top] = screen_type;
        g_current_screen = screen_type;
        ESP_LOGI(TAG, "Pushed screen %d to stack (top=%d)", screen_type, g_navigation_stack_top);
    } else {
        ESP_LOGW(TAG, "Navigation stack full, replacing top");
        g_navigation_stack[g_navigation_stack_top] = screen_type;
        g_current_screen = screen_type;
    }
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

    // 首页总是作为栈底，清空栈
    g_navigation_stack[0] = SCREEN_TYPE_INDEX;
    g_navigation_stack_top = 0;
    g_current_screen = SCREEN_TYPE_INDEX;

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

    // 压入导航栈
    push_screen(SCREEN_TYPE_FILE_BROWSER);

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

    // 压入导航栈
    push_screen(SCREEN_TYPE_SETTINGS);

    settings_screen_create(g_context->indev);
}

void screen_manager_show_reader(const char *file_path)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return;
    }

    if (file_path == NULL) {
        ESP_LOGE(TAG, "File path is NULL");
        return;
    }

    ESP_LOGI(TAG, "Navigating to reader screen: %s", file_path);

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();

    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    extern void reader_screen_create_wrapper(const char *file_path, lv_indev_t *indev);

    // 压入导航栈
    push_screen(SCREEN_TYPE_READER);

    reader_screen_create_wrapper(file_path, g_context->indev);
}

void screen_manager_show_image_browser(const char *directory)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return;
    }

    if (directory == NULL) {
        ESP_LOGE(TAG, "Directory is NULL");
        return;
    }

    ESP_LOGI(TAG, "Navigating to image browser screen: %s", directory);

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();

    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    extern void image_browser_screen_create(const char *directory, int start_index, lv_indev_t *indev);

    // 压入导航栈
    push_screen(SCREEN_TYPE_IMAGE_BROWSER);

    image_browser_screen_create(directory, 0, g_context->indev);
}

bool screen_manager_go_back(void)
{
    if (g_context == NULL) {
        ESP_LOGE(TAG, "Screen manager not initialized!");
        return false;
    }

    // 如果已经在首页，无法返回
    if (g_navigation_stack_top == 0) {
        ESP_LOGI(TAG, "Already at index screen, cannot go back");
        return false;
    }

    // 弹出栈顶
    g_navigation_stack_top--;
    screen_type_t prev_screen = g_navigation_stack[g_navigation_stack_top];
    g_current_screen = prev_screen;

    ESP_LOGI(TAG, "Going back to screen %d (stack top: %d)", prev_screen, g_navigation_stack_top);

    // 重置刷新状态，确保新的屏幕刷新不受旧状态影响
    lvgl_reset_refresh_state();

    // 组件间切换：强制使用全刷模式，确保屏幕完全清晰
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);

    // 根据屏幕类型切换
    switch (prev_screen) {
        case SCREEN_TYPE_INDEX:
            {
                extern void index_screen_create(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str, lv_indev_t *indev);
                index_screen_create(
                    g_context->battery_mv,
                    g_context->battery_pct,
                    g_context->charging,
                    g_context->version_str,
                    g_context->indev
                );
            }
            break;
        case SCREEN_TYPE_FILE_BROWSER:
            {
                extern void file_browser_screen_create(lv_indev_t *indev);
                file_browser_screen_create(g_context->indev);
            }
            break;
        case SCREEN_TYPE_READER:
            {
                // 阅读器屏幕需要文件路径，但返回时不应该再次打开
                // 这里我们返回到文件浏览器
                ESP_LOGW(TAG, "Cannot return to reader screen without file path, redirecting to file browser");
                extern void file_browser_screen_create(lv_indev_t *indev);
                file_browser_screen_create(g_context->indev);
            }
            break;
        case SCREEN_TYPE_SETTINGS:
            {
                extern void settings_screen_create(lv_indev_t *indev);
                settings_screen_create(g_context->indev);
            }
            break;
        case SCREEN_TYPE_IMAGE_BROWSER:
            {
                // 图片浏览器返回时回到文件浏览器
                extern void file_browser_screen_create(lv_indev_t *indev);
                file_browser_screen_create(g_context->indev);
            }
            break;
        default:
            ESP_LOGE(TAG, "Unknown screen type: %d", prev_screen);
            return false;
    }

    return true;
}

screen_type_t screen_manager_get_current_screen(void)
{
    return g_current_screen;
}

screen_context_t* screen_manager_get_context(void)
{
    return g_context;
}
