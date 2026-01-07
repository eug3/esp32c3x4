/**
 * @file boot_screen.c
 * @brief 启动屏幕实现 - 持续显示动画直到初始化完成
 */

#include "boot_screen.h"
#include "display_engine.h"
#include "boot_animation.h"
#include "boot_animation_frames.h"
#include "Fonts/fonts.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BOOT_SCREEN";

// 启动屏幕实例
static screen_t g_boot_screen = {0};

// 状态
static char s_status_text[64] = "Initializing...";
static bool s_completed = false;
static int s_current_frame = 0;

// 屏幕上下文
static screen_context_t *s_context = NULL;

// ============================================================================
// 生命周期函数
// ============================================================================

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Boot screen shown");
    s_context = screen_manager_get_context();
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Boot screen hidden");
}

static void on_draw(screen_t *screen)
{
    ESP_LOGI(TAG, "on_draw START");

    // 清空屏幕
    display_clear(COLOR_WHITE);

    // 绘制动画帧
    const int anim_x = (SCREEN_WIDTH - BOOT_ANIM_FRAME_WIDTH) / 2;
    const int anim_y = 120;
    const int padding = 12;

    display_draw_bitmap_mask_1bpp(
        anim_x,
        anim_y,
        BOOT_ANIM_FRAME_WIDTH,
        BOOT_ANIM_FRAME_HEIGHT,
        g_boot_anim_frames[s_current_frame],
        BOOT_ANIM_FRAME_STRIDE_BYTES,
        COLOR_BLACK);

    // 绘制状态文本
    sFONT *status_font = &Font16;
    const int status_y = anim_y + BOOT_ANIM_FRAME_HEIGHT + padding;
    int text_w = display_get_text_width_font(s_status_text, status_font);
    int text_x = (SCREEN_WIDTH - text_w) / 2;
    if (text_x < 0) text_x = 0;

    display_draw_text_font(text_x, status_y, s_status_text, status_font, COLOR_BLACK, COLOR_WHITE);

    // 刷新显示
    display_refresh(REFRESH_MODE_PARTIAL);

    // 准备下一帧
    s_current_frame = (s_current_frame + 1) % BOOT_ANIM_FRAME_COUNT;

    ESP_LOGI(TAG, "on_draw END");
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    // 启动屏幕不接受用户输入
    (void)screen;
    (void)btn;
    (void)event;
}

// ============================================================================
// 公共接口
// ============================================================================

void boot_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing boot screen");

    g_boot_screen.name = "boot";
    g_boot_screen.user_data = NULL;
    g_boot_screen.on_show = on_show;
    g_boot_screen.on_hide = on_hide;
    g_boot_screen.on_draw = on_draw;
    g_boot_screen.on_event = on_event;
    g_boot_screen.is_visible = false;
    g_boot_screen.needs_redraw = true;  // 启动屏幕总是需要重绘

    ESP_LOGI(TAG, "Boot screen initialized");
}

screen_t* boot_screen_get_instance(void)
{
    if (g_boot_screen.name == NULL) {
        boot_screen_init();
    }
    return &g_boot_screen;
}

void boot_screen_set_status(const char *status)
{
    if (status == NULL) {
        return;
    }
    strncpy(s_status_text, status, sizeof(s_status_text) - 1);
    s_status_text[sizeof(s_status_text) - 1] = '\0';

    // 请求重绘以显示新状态
    g_boot_screen.needs_redraw = true;
}

void boot_screen_complete(void)
{
    ESP_LOGI(TAG, "Boot initialization complete");
    s_completed = true;
    snprintf(s_status_text, sizeof(s_status_text), "Starting...");
    g_boot_screen.needs_redraw = true;
}

bool boot_screen_is_completed(void)
{
    return s_completed;
}
