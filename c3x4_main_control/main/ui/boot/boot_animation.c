#include "boot_animation.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_engine.h"
#include "boot_animation_frames.h"
#include "Fonts/fonts.h"

#define BOOT_ANIM_FRAME_X ((SCREEN_WIDTH - BOOT_ANIM_FRAME_WIDTH) / 2)
#define BOOT_ANIM_FRAME_Y (120)

static void clamp_region(int *x, int *y, int *w, int *h)
{
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > SCREEN_WIDTH) {
        *w = SCREEN_WIDTH - *x;
    }
    if (*y + *h > SCREEN_HEIGHT) {
        *h = SCREEN_HEIGHT - *y;
    }
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static void draw_status_line(const char *status, int y)
{
    if (!status) {
        return;
    }

    // 启动动画阶段不使用汉字字体；避免在 SD 挂载前触发字体文件访问。
    sFONT *status_font = &Font16;

    int text_w = display_get_text_width_font(status, status_font);
    int x = (SCREEN_WIDTH - text_w) / 2;
    if (x < 0) x = 0;

    display_draw_text_font(x, y, status, status_font, COLOR_BLACK, COLOR_WHITE);
}

void boot_animation_show(const char *status, int frame_index)
{
    if (frame_index < 0) {
        frame_index = 0;
    }
    frame_index %= BOOT_ANIM_FRAME_COUNT;

    // 启动动画阶段不使用汉字字体；避免在 SD 挂载前触发字体文件访问。
    const int status_h = display_get_text_height_font(&Font16);
    const int padding = 12;
    const int status_y = BOOT_ANIM_FRAME_Y + BOOT_ANIM_FRAME_HEIGHT + padding;

    int rx = BOOT_ANIM_FRAME_X;
    int ry = BOOT_ANIM_FRAME_Y;
    int rw = BOOT_ANIM_FRAME_WIDTH;
    int rh = BOOT_ANIM_FRAME_HEIGHT + padding + status_h + padding;
    clamp_region(&rx, &ry, &rw, &rh);

    display_clear_region(rx, ry, rw, rh, COLOR_WHITE);

    display_draw_bitmap_mask_1bpp(
        BOOT_ANIM_FRAME_X,
        BOOT_ANIM_FRAME_Y,
        BOOT_ANIM_FRAME_WIDTH,
        BOOT_ANIM_FRAME_HEIGHT,
        g_boot_anim_frames[frame_index],
        BOOT_ANIM_FRAME_STRIDE_BYTES,
        COLOR_BLACK);

    // draw_status_line 内部会使用固定的 ASCII 字体。
    draw_status_line(status, status_y);

    display_refresh_region(rx, ry, rw, rh, REFRESH_MODE_PARTIAL);
}

void boot_animation_play_ms(const char *status, uint32_t duration_ms)
{
    const uint32_t step_ms = 180;

    if (duration_ms == 0) {
        boot_animation_show(status, 0);
        return;
    }

    uint32_t elapsed = 0;
    int frame = 0;
    while (elapsed < duration_ms) {
        boot_animation_show(status, frame);
        frame = (frame + 1) % BOOT_ANIM_FRAME_COUNT;

        const uint32_t sleep_ms = (duration_ms - elapsed < step_ms) ? (duration_ms - elapsed) : step_ms;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        elapsed += sleep_ms;
    }
}
