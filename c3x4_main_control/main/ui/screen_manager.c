/**
 * @file screen_manager.c
 * @brief 屏幕导航管理器实现（手绘 UI 版本，无 LVGL 依赖）
 */

#include "screen_manager.h"
#include "display_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SCREEN_MGR";

// 屏幕管理器全局状态
static screen_manager_t g_mgr = {0};
static bool g_initialized = false;

// 导航栈
static screen_t *g_nav_stack[NAV_STACK_DEPTH] = {0};
static int g_nav_stack_top = -1;

// 外部屏幕声明（将在各屏幕模块中定义）
extern screen_t g_home_screen;
extern screen_t g_file_browser_screen;
extern screen_t g_reader_screen;
extern screen_t g_settings_screen;
extern screen_t g_image_viewer_screen;
extern screen_t g_ble_reader_screen;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void push_nav_stack(screen_t *screen);
static screen_t* pop_nav_stack(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static void push_nav_stack(screen_t *screen)
{
    if (g_nav_stack_top < NAV_STACK_DEPTH - 1) {
        g_nav_stack_top++;
        g_nav_stack[g_nav_stack_top] = screen;
        ESP_LOGI(TAG, "Pushed screen '%s' to nav stack (top=%d)",
                 screen ? screen->name : "NULL", g_nav_stack_top);
    } else {
        ESP_LOGW(TAG, "Nav stack full, replacing top");
        g_nav_stack[g_nav_stack_top] = screen;
    }
}

static screen_t* pop_nav_stack(void)
{
    if (g_nav_stack_top < 0) {
        return NULL;
    }
    screen_t *screen = g_nav_stack[g_nav_stack_top];
    g_nav_stack_top--;
    ESP_LOGI(TAG, "Popped screen '%s' from nav stack (top=%d)",
             screen ? screen->name : "NULL", g_nav_stack_top);
    return screen;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool screen_manager_init(screen_context_t *ctx)
{
    if (g_initialized) {
        ESP_LOGW(TAG, "Screen manager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing screen manager...");

    // 清零状态
    memset(&g_mgr, 0, sizeof(g_mgr));
    g_nav_stack_top = -1;

    // 保存上下文
    g_mgr.context = ctx;

    // 注册屏幕（这些屏幕将在各自的模块中定义）
    // screen_manager_register(&g_home_screen);
    // screen_manager_register(&g_file_browser_screen);
    // ...

    g_initialized = true;
    ESP_LOGI(TAG, "Screen manager initialized");

    return true;
}

void screen_manager_deinit(void)
{
    if (!g_initialized) {
        return;
    }

    // 隐藏当前屏幕
    if (g_mgr.current_screen != NULL &&
        g_mgr.current_screen->on_hide != NULL) {
        g_mgr.current_screen->on_hide(g_mgr.current_screen);
    }

    memset(&g_mgr, 0, sizeof(g_mgr));
    g_nav_stack_top = -1;
    g_initialized = false;

    ESP_LOGI(TAG, "Screen manager deinitialized");
}

bool screen_manager_register(screen_t *screen)
{
    if (screen == NULL) {
        ESP_LOGE(TAG, "Cannot register NULL screen");
        return false;
    }

    if (g_mgr.screen_count >= MAX_SCREENS) {
        ESP_LOGE(TAG, "Max screens reached (%d)", MAX_SCREENS);
        return false;
    }

    // 检查是否已注册
    for (int i = 0; i < g_mgr.screen_count; i++) {
        if (g_mgr.screens[i] == screen) {
            ESP_LOGW(TAG, "Screen '%s' already registered", screen->name);
            return true;
        }
    }

    g_mgr.screens[g_mgr.screen_count++] = screen;
    ESP_LOGI(TAG, "Registered screen '%s'", screen->name);

    return true;
}

void screen_manager_unregister(screen_t *screen)
{
    if (screen == NULL) {
        return;
    }

    // 从数组中移除
    for (int i = 0; i < g_mgr.screen_count; i++) {
        if (g_mgr.screens[i] == screen) {
            // 移动后续元素
            for (int j = i; j < g_mgr.screen_count - 1; j++) {
                g_mgr.screens[j] = g_mgr.screens[j + 1];
            }
            g_mgr.screen_count--;
            ESP_LOGI(TAG, "Unregistered screen '%s'", screen->name);
            return;
        }
    }

    ESP_LOGW(TAG, "Screen '%s' not found", screen->name);
}

bool screen_manager_show(const char *screen_name)
{
    screen_t *screen = screen_manager_find(screen_name);
    if (screen == NULL) {
        ESP_LOGE(TAG, "Screen '%s' not found", screen_name);
        return false;
    }

    return screen_manager_show_screen(screen);
}

bool screen_manager_show_screen(screen_t *screen)
{
    if (screen == NULL) {
        ESP_LOGE(TAG, "Cannot show NULL screen");
        return false;
    }

    ESP_LOGI(TAG, "Showing screen '%s'", screen->name);

    // 隐藏当前屏幕
    if (g_mgr.current_screen != NULL &&
        g_mgr.current_screen->on_hide != NULL) {
        g_mgr.current_screen->on_hide(g_mgr.current_screen);
    }

    // 压入导航栈
    push_nav_stack(screen);

    // 切换到新屏幕
    g_mgr.current_screen = screen;
    screen->is_visible = true;
    screen->needs_redraw = true;

    // 调用 on_show 回调
    if (screen->on_show != NULL) {
        screen->on_show(screen);
    }

    // 绘制屏幕
    screen_manager_draw();

    // 刷新显示
    ESP_LOGI(TAG, "Calling display_refresh...");
    display_refresh(REFRESH_MODE_FULL);
    ESP_LOGI(TAG, "display_refresh returned, screen_manager_show complete");

    return true;
}

bool screen_manager_back(void)
{
    if (g_nav_stack_top <= 0) {
        ESP_LOGI(TAG, "Already at first screen, cannot go back");
        return false;
    }

    // 弹出当前屏幕
    screen_t *current = pop_nav_stack();

    // 隐藏当前屏幕
    if (current != NULL && current->on_hide != NULL) {
        current->on_hide(current);
        current->is_visible = false;
    }

    // 获取上一个屏幕
    screen_t *prev = g_nav_stack[g_nav_stack_top];
    if (prev == NULL) {
        ESP_LOGE(TAG, "Previous screen is NULL");
        return false;
    }

    ESP_LOGI(TAG, "Going back to screen '%s'", prev->name);

    // 切换到上一个屏幕
    g_mgr.current_screen = prev;
    prev->is_visible = true;
    prev->needs_redraw = true;

    // 调用 on_show 回调
    if (prev->on_show != NULL) {
        prev->on_show(prev);
    }

    // 绘制屏幕
    screen_manager_draw();

    // 刷新显示
    display_refresh(REFRESH_MODE_FULL);

    return true;
}

screen_t* screen_manager_get_current(void)
{
    return g_mgr.current_screen;
}

screen_t* screen_manager_find(const char *screen_name)
{
    if (screen_name == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_mgr.screen_count; i++) {
        if (g_mgr.screens[i] != NULL &&
            strcmp(g_mgr.screens[i]->name, screen_name) == 0) {
            return g_mgr.screens[i];
        }
    }

    return NULL;
}

void screen_manager_request_redraw(void)
{
    if (g_mgr.current_screen != NULL) {
        g_mgr.current_screen->needs_redraw = true;
    }
}

bool screen_manager_handle_event(button_t btn, button_event_t event)
{
    if (g_mgr.current_screen == NULL) {
        return false;
    }

    if (g_mgr.current_screen->on_event != NULL) {
        g_mgr.current_screen->on_event(g_mgr.current_screen, btn, event);
        return true;  // 事件已处理
    }

    return false;  // 事件未处理
}

void screen_manager_draw(void)
{
    if (g_mgr.current_screen == NULL) {
        return;
    }

    if (g_mgr.current_screen->needs_redraw &&
        g_mgr.current_screen->on_draw != NULL) {
        g_mgr.current_screen->on_draw(g_mgr.current_screen);
        g_mgr.current_screen->needs_redraw = false;
    }
}

const screen_manager_t* screen_manager_get_state(void)
{
    return &g_mgr;
}

screen_context_t* screen_manager_get_context(void)
{
    return g_mgr.context;
}

// 兼容旧 API 的包装函数

void screen_manager_show_index(void)
{
    screen_manager_show("home");
}

void screen_manager_show_file_browser(void)
{
    screen_manager_show("file_browser");
}

void screen_manager_show_settings(void)
{
    screen_manager_show("settings");
}

void screen_manager_show_ble_reader(void)
{
    screen_manager_show("ble_reader");
}

void screen_manager_show_reader(const char *file_path)
{
    screen_t *screen = screen_manager_find("reader");
    if (screen != NULL) {
        // 保存文件路径到屏幕的用户数据
        screen->user_data = (void*)file_path;
        screen_manager_show_screen(screen);
    }
}

void screen_manager_show_image_browser(const char *file_path)
{
    screen_t *screen = screen_manager_find("image_viewer");
    if (screen != NULL) {
        // 保存完整文件路径到屏幕的用户数据
        screen->user_data = (void*)file_path;
        screen_manager_show_screen(screen);
    }
}

bool screen_manager_go_back(void)
{
    return screen_manager_back();
}
