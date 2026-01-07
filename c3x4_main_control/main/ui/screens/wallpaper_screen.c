/**
 * @file wallpaper_screen.c
 * @brief 壁纸管理：列表浏览 SD 卡图片，进入预览，确认设为壁纸
 */

#include "wallpaper_screen.h"
#include "wallpaper_manager.h"
#include "paginated_menu.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "jpeg_helper.h"
#include "bmp_helper.h"
#include "png_helper.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "WALLPAPER_SCREEN";
static screen_t g_wallpaper_screen = {0};

typedef enum {
    MODE_LIST = 0,
    MODE_PREVIEW
} wp_mode_t;

static struct {
    wp_mode_t mode;
    paginated_menu_t menu;
    wallpaper_list_t list;   // 来自 SD 的图片列表
    int current_index;       // 预览模式下的当前索引
} s_wp = {0};

static bool menu_item_getter(int index, char *out_text, int out_text_size, bool *out_is_selected)
{
    if (index < 0 || index >= s_wp.list.count) return false;
    const char *name = s_wp.list.items[index].name;
    strncpy(out_text, name, out_text_size - 1);
    out_text[out_text_size - 1] = '\0';
    int selected_index = paginated_menu_get_selected_index(&s_wp.menu);
    *out_is_selected = (index == selected_index);
    return true;
}

static bool render_fullscreen_image(const char *full_path)
{
    ESP_LOGI(TAG, "Preview: %s", full_path);

    FILE *f = fopen(full_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Open failed: %s", full_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 8*1024*1024) { // 简单上限防护
        fclose(f);
        ESP_LOGE(TAG, "Invalid size: %ld", size);
        return false;
    }
    uint8_t *buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, size, f);
    fclose(f);
    if (rd != size) { free(buf); return false; }

    display_clear(COLOR_WHITE);

    const char *ext = strrchr(full_path, '.');
    bool ok = false;
    if (ext) {
        ext++;
        if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
            ok = jpeg_helper_render_fullscreen(buf, size);
        } else if (!strcasecmp(ext, "bmp")) {
            ok = bmp_helper_render_fullscreen(buf, size);
        } else if (!strcasecmp(ext, "png")) {
            ok = png_helper_render_fullscreen(buf, size);
        } else {
            display_draw_text_menu(20, 200, "不支持的图片格式", COLOR_BLACK, COLOR_WHITE);
            ok = true;
        }
    }

    // 底部提示
    display_draw_text_menu(20, SCREEN_HEIGHT - 100, "左右: 切换  返回: 返回  确认: 设为壁纸", COLOR_BLACK, COLOR_WHITE);
    display_refresh(REFRESH_MODE_FULL);

    free(buf);
    return ok;
}

static void enter_list_mode(void)
{
    s_wp.mode = MODE_LIST;
    g_wallpaper_screen.needs_redraw = true;
}

static void enter_preview_mode(int index)
{
    if (index < 0 || index >= s_wp.list.count) return;
    s_wp.mode = MODE_PREVIEW;
    s_wp.current_index = index;

    char full[256];
    snprintf(full, sizeof(full), "%s", s_wp.list.items[index].path);
    render_fullscreen_image(full);
}

// SCREEN callbacks
static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Show wallpaper screen");
    // 准备菜单
    paginated_menu_config_t cfg = {
        .start_y = 80,
        .item_height = 50,
        .bottom_margin = 80,
        .menu_width = 400,
        .text_offset_y = 12,
        .items_per_page = 10,
        .item_getter = menu_item_getter,
        .item_drawer = NULL,
        .user_data = NULL,
        .padding_x = 10,
        .padding_y = 5,
        .show_page_hint = true,
        .page_hint_x = -1,
        .page_hint_y = -1
    };
    paginated_menu_init(&s_wp.menu, &cfg);

    // 扫描 SD 卡图片列表（不做缓存导入）
    memset(&s_wp.list, 0, sizeof(s_wp.list));
    wallpaper_scan_sdcard(&s_wp.list);
    paginated_menu_set_total_count(&s_wp.menu, s_wp.list.count);
    paginated_menu_set_selected_index(&s_wp.menu, 0);

    enter_list_mode();
}

static void on_hide(screen_t *screen)
{
    paginated_menu_deinit(&s_wp.menu);
    wallpaper_list_free(&s_wp.list);
}

static void on_draw(screen_t *screen)
{
    if (s_wp.mode == MODE_LIST) {
        display_clear(COLOR_WHITE);
        display_draw_text_menu(20, 20, "壁纸管理", COLOR_BLACK, COLOR_WHITE);
        paginated_menu_draw(&s_wp.menu);
        paginated_menu_draw_footer_hint(&s_wp.menu, "上下: 选择  确认: 预览  返回: 返回", 20, SCREEN_HEIGHT - 60);
        display_refresh(REFRESH_MODE_PARTIAL);
    } else {
        // 预览模式下在 enter_preview_mode 里已绘制
    }
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) return;

    if (s_wp.mode == MODE_LIST) {
        if (btn == BTN_LEFT || btn == BTN_RIGHT || btn == BTN_VOLUME_UP || btn == BTN_VOLUME_DOWN) {
            bool changed = paginated_menu_handle_button(&s_wp.menu, btn, NULL, NULL);
            if (!changed && (btn == BTN_LEFT || btn == BTN_RIGHT)) {
                int delta = (btn == BTN_LEFT) ? -1 : 1;
                changed = paginated_menu_move_selection(&s_wp.menu, delta);
            }
            if (changed) screen->needs_redraw = true;
            return;
        }
        if (btn == BTN_CONFIRM) {
            int idx = paginated_menu_get_selected_index(&s_wp.menu);
            enter_preview_mode(idx);
            return;
        }
        if (btn == BTN_BACK) {
            screen_manager_back();
            return;
        }
    } else { // MODE_PREVIEW
        if (btn == BTN_LEFT || btn == BTN_RIGHT) {
            if (s_wp.list.count > 0) {
                s_wp.current_index += (btn == BTN_LEFT) ? -1 : 1;
                if (s_wp.current_index < 0) s_wp.current_index = s_wp.list.count - 1;
                if (s_wp.current_index >= s_wp.list.count) s_wp.current_index = 0;
                render_fullscreen_image(s_wp.list.items[s_wp.current_index].path);
            }
            return;
        }
        if (btn == BTN_BACK) {
            enter_list_mode();
            screen->needs_redraw = true;
            return;
        }
        if (btn == BTN_CONFIRM) {
            const char *full_path = s_wp.list.items[s_wp.current_index].path;
            if (wallpaper_select_path(full_path)) {
                display_draw_text_menu(20, SCREEN_HEIGHT - 140, "已设为壁纸", COLOR_BLACK, COLOR_WHITE);
                display_refresh(REFRESH_MODE_PARTIAL);
            }
            return;
        }
    }
}

void wallpaper_screen_init(void)
{
    g_wallpaper_screen.name = "wallpaper";
    g_wallpaper_screen.user_data = NULL;
    g_wallpaper_screen.on_show = on_show;
    g_wallpaper_screen.on_hide = on_hide;
    g_wallpaper_screen.on_draw = on_draw;
    g_wallpaper_screen.on_event = on_event;
    g_wallpaper_screen.is_visible = false;
    g_wallpaper_screen.needs_redraw = false;
}

screen_t* wallpaper_screen_get_instance(void)
{
    if (g_wallpaper_screen.name == NULL) {
        wallpaper_screen_init();
    }
    return &g_wallpaper_screen;
}
