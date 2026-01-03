/**
 * @file settings_screen.c
 * @brief 设置页面实现
 */

#include "settings_screen.h"
#include "../lvgl_driver.h"
#include "screen_manager.h"
#include "font_manager.h"
#include "font_loader.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SETTINGS_SCR";

// 设置页面状态
typedef struct {
    lv_obj_t *font_list;              // 字体列表
    lv_obj_t *font_buttons[10];       // 字体选择按钮
    int font_button_count;            // 字体按钮数量
    int selected_font_index;          // 当前选中的字体索引

    lv_indev_t *indev;
    lv_group_t *group;
} settings_state_t;

static settings_state_t g_settings = {0};

// 前置声明
static void settings_font_button_event_cb(lv_event_t *e);
static void settings_font_button_focused_cb(lv_event_t *e);
static void settings_screen_destroy_cb(lv_event_t *e);
static void settings_key_event_cb(lv_event_t *e);

// 设置按钮选中状态
static void set_font_button_selected(lv_obj_t *btn, bool selected)
{
    if (btn == NULL) {
        return;
    }

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label == NULL) {
        return;
    }

    if (selected) {
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
    }
}

// 刷新字体列表显示
static void update_font_list_display(void)
{
    if (g_settings.font_list == NULL) {
        return;
    }

    // 重建 group
    if (g_settings.indev != NULL) {
        if (g_settings.group != NULL) {
            lv_group_del(g_settings.group);
            g_settings.group = NULL;
        }
        g_settings.group = lv_group_create();
        lv_group_set_wrap(g_settings.group, true);
        lv_indev_set_group(g_settings.indev, g_settings.group);
    }

    // 清空列表
    lv_obj_clean(g_settings.font_list);

    g_settings.font_button_count = 0;

    // 获取字体列表
    const font_info_t *font_list = font_manager_get_font_list();
    int font_count = font_manager_get_font_count();

    // 添加默认字体选项
    lv_obj_t *btn = lv_list_add_button(g_settings.font_list, LV_SYMBOL_SETTINGS, "Default (Montserrat)");
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_user_data(btn, (void *)(uintptr_t)-1);  // 存储字体索引

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_obj_t *icon = lv_obj_get_child(btn, 1);
    if (label) {
        lv_obj_set_style_text_font(label, font_manager_get_font(), 0);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
    }
    if (icon) {
        lv_obj_set_style_text_color(icon, lv_color_black(), 0);
    }

    lv_obj_add_event_cb(btn, settings_font_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)-1);
    lv_obj_add_event_cb(btn, settings_font_button_focused_cb, LV_EVENT_FOCUSED, (void *)(uintptr_t)-1);
    g_settings.font_buttons[g_settings.font_button_count] = btn;
    g_settings.font_button_count++;
    if (g_settings.group) {
        lv_group_add_obj(g_settings.group, btn);
    }

    // 添加所有字体选项
    for (int i = 0; i < font_count && g_settings.font_button_count < 10; i++) {
        char btn_text[128];
        snprintf(btn_text, sizeof(btn_text), "%s", font_list[i].name);

        btn = lv_list_add_button(g_settings.font_list, LV_SYMBOL_FILE, btn_text);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);  // 存储字体索引

        // 使用当前选择的字体显示按钮文本
        label = lv_obj_get_child(btn, 0);
        icon = lv_obj_get_child(btn, 1);
        if (label) {
            lv_obj_set_style_text_font(label, font_manager_get_font(), 0);
            lv_obj_set_style_text_color(label, lv_color_black(), 0);
        }
        if (icon) {
            lv_obj_set_style_text_color(icon, lv_color_black(), 0);
        }

        lv_obj_add_event_cb(btn, settings_font_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(btn, settings_font_button_focused_cb, LV_EVENT_FOCUSED, (void *)(uintptr_t)i);
        g_settings.font_buttons[g_settings.font_button_count] = btn;
        g_settings.font_button_count++;
        if (g_settings.group) {
            lv_group_add_obj(g_settings.group, btn);
        }

        // 设置选中状态
        if (i == g_settings.selected_font_index) {
            set_font_button_selected(btn, true);
        }
    }

    // 如果选择的是默认字体，高亮默认选项
    if (g_settings.selected_font_index == -1 && g_settings.font_button_count > 0) {
        set_font_button_selected(g_settings.font_buttons[0], true);
    }

    // 触发渲染
    for (int i = 0; i < 3; i++) {
        lvgl_trigger_render(NULL);
    }
}

// 字体按钮点击事件
static void settings_font_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const int font_index = (int)(uintptr_t)lv_event_get_user_data(e);

    ESP_LOGI(TAG, "Font button clicked: index=%d", font_index);

    // 更新选中状态
    for (int i = 0; i < g_settings.font_button_count; i++) {
        set_font_button_selected(g_settings.font_buttons[i], false);
    }

    // 找到对应的按钮并设置选中状态
    for (int i = 0; i < g_settings.font_button_count; i++) {
        int btn_index = (int)(uintptr_t)lv_obj_get_user_data(g_settings.font_buttons[i]);
        if (btn_index == font_index) {
            set_font_button_selected(g_settings.font_buttons[i], true);
            break;
        }
    }

    // 设置字体
    if (font_index == -1) {
        // 默认字体
        font_manager_set_font((lv_font_t *)&lv_font_montserrat_14);
        g_settings.selected_font_index = -1;
    } else {
        font_manager_set_font_by_index(font_index);
        g_settings.selected_font_index = font_index;
    }

    // 保存选择
    font_manager_save_selection();

    // 刷新显示
    update_font_list_display();

    // 触发 EPD 刷新
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_display_refresh();
}

// 字体按钮焦点事件
static void settings_font_button_focused_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) {
        return;
    }

    // 触发渲染
    lvgl_trigger_render(NULL);
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_display_refresh();
}

// 按键事件（ESC 返回）
static void settings_key_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_ESC) {
        ESP_LOGI(TAG, "Exiting settings screen");
        lvgl_reset_refresh_state();
        screen_manager_show_index();
    }
}

// 屏幕销毁回调
static void settings_screen_destroy_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Settings screen destroyed");

    if (g_settings.group != NULL) {
        lv_group_del(g_settings.group);
        g_settings.group = NULL;
    }

    memset(&g_settings, 0, sizeof(settings_state_t));
}

void settings_screen_create(lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Creating settings screen");

    // 初始化状态
    memset(&g_settings, 0, sizeof(settings_state_t));
    g_settings.indev = indev;
    g_settings.selected_font_index = -1;

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 注册屏幕销毁回调
    lv_obj_add_event_cb(screen, settings_screen_destroy_cb, LV_EVENT_DELETE, NULL);

    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // 添加全局按键事件处理（用于 ESC 返回）
    lv_obj_add_event_cb(screen, settings_key_event_cb, LV_EVENT_KEY, NULL);

    // ========================================
    // 顶部标题
    // ========================================
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *line_top = lv_line_create(screen);
    static lv_point_precise_t line_top_points[] = {{10, 40}, {470, 40}};
    lv_line_set_points(line_top, line_top_points, 2);
    lv_obj_set_style_line_width(line_top, 2, 0);
    lv_obj_set_style_line_color(line_top, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_top, LV_OPA_COVER, 0);

    // ========================================
    // 字体选择区域
    // ========================================
    lv_obj_t *font_title = lv_label_create(screen);
    lv_label_set_text(font_title, "Font Selection:");
    lv_obj_set_style_text_font(font_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(font_title, lv_color_black(), 0);
    lv_obj_align(font_title, LV_ALIGN_TOP_LEFT, 20, 60);

    // 字体列表
    g_settings.font_list = lv_list_create(screen);
    lv_obj_set_size(g_settings.font_list, 440, 600);
    lv_obj_align(g_settings.font_list, LV_ALIGN_TOP_LEFT, 20, 90);

    lv_obj_set_style_bg_color(g_settings.font_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_settings.font_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_settings.font_list, 1, 0);
    lv_obj_set_style_border_color(g_settings.font_list, lv_color_black(), 0);
    lv_obj_set_style_text_color(g_settings.font_list, lv_color_black(), LV_PART_ITEMS);

    // ========================================
    // 底部操作提示
    // ========================================
    lv_obj_t *line_bottom = lv_line_create(screen);
    static lv_point_precise_t line_bottom_points[] = {{10, 720}, {470, 720}};
    lv_line_set_points(line_bottom, line_bottom_points, 2);
    lv_obj_set_style_line_width(line_bottom, 2, 0);
    lv_obj_set_style_line_color(line_bottom, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_bottom, LV_OPA_COVER, 0);

    lv_obj_t *hint1 = lv_label_create(screen);
    lv_label_set_text(hint1, "Vol+/-: Select font");
    lv_obj_set_style_text_font(hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint1, lv_color_black(), 0);
    lv_obj_align(hint1, LV_ALIGN_TOP_LEFT, 20, 730);

    lv_obj_t *hint2 = lv_label_create(screen);
    lv_label_set_text(hint2, "Confirm(3): Apply font");
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint2, lv_color_black(), 0);
    lv_obj_align(hint2, LV_ALIGN_TOP_LEFT, 20, 750);

    lv_obj_t *hint3 = lv_label_create(screen);
    lv_label_set_text(hint3, "Back(4): Return");
    lv_obj_set_style_text_font(hint3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint3, lv_color_black(), 0);
    lv_obj_align(hint3, LV_ALIGN_TOP_LEFT, 20, 770);

    // 填充字体列表
    update_font_list_display();

    // 刷新显示
    lv_obj_invalidate(screen);
    for (int i = 0; i < 5; i++) {
        lvgl_trigger_render(NULL);
    }

    // 初始刷新 EPD - 由 screen_manager 设置刷新模式（组件间切换用 FULL）
    lvgl_display_refresh();

    ESP_LOGI(TAG, "Settings screen created successfully");
}
