/**
 * @file index_screen.c
 * @brief 首页 - Monster For Pan 主菜单
 */

#include "index_screen.h"
#include "../lvgl_driver.h"
#include "core/lv_obj_style_gen.h"
#include "screen_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "INDEX_SCR";

// 菜单按钮指针数组
static lv_obj_t *index_menu_buttons[3] = {NULL, NULL, NULL};

// 记录上一个焦点的按钮，用于局部刷新
static lv_obj_t *s_last_focused_button = NULL;

// 前置声明
static void index_menu_button_event_cb(lv_event_t *e);
static void index_button_key_event_cb(lv_event_t *e);
static void index_button_focus_event_cb(lv_event_t *e);
static void index_screen_destroy_cb(lv_event_t *e);

// 保存 group 指针用于调试
static lv_group_t *s_index_group = NULL;

static void index_activate_menu(uint16_t menu_index)
{
    ESP_LOGI(TAG, "Menu activated: %u", menu_index);

    switch (menu_index) {
        case 0:  // SDCard File Browser
            ESP_LOGI(TAG, "Launching SD Card File Browser...");
            lvgl_reset_refresh_state();
            screen_manager_show_file_browser();
            break;
        case 1:  // BLE Reader
            ESP_LOGI(TAG, "BLE Reader selected (not implemented yet)");
            break;
        case 2:  // Settings
            ESP_LOGI(TAG, "Launching Settings...");
            lvgl_reset_refresh_state();
            screen_manager_show_settings();
            break;
        default:
            ESP_LOGW(TAG, "Unknown menu index: %u", menu_index);
            break;
    }
}

// 按钮焦点变化事件 - 当按钮获得或失去焦点时触发
static void index_button_focus_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        ESP_LOGI(TAG, "Button focused: %p", btn);

        // 找到是哪个按钮
        for (int i = 0; i < 3; i++) {
            if (index_menu_buttons[i] == btn) {
                ESP_LOGI(TAG, "Button %d gained FOCUS", i);
                break;
            }
        }

        // 组件内操作：使用局刷模式（PARTIAL）提升响应速度
        // 焦点切换是小范围更新，局刷足够且更快
        // 重要：必须先设置刷新模式，再触发渲染，否则脏区不会被记录
        lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);

        // 手动刷新模式(RENDER_MODE_DIRECT): 必须手动触发渲染和刷新
        // lv_obj_invalidate() 只标记需要重绘,但不会自动触发flush_cb
        lv_obj_invalidate(btn);

        // 触发LVGL渲染
        lvgl_trigger_render(NULL);

        // 记录当前焦点按钮
        s_last_focused_button = btn;

        // 触发EPD刷新
        lvgl_display_refresh();
    }
    else if (code == LV_EVENT_DEFOCUSED) {
        ESP_LOGI(TAG, "Button defocused: %p", btn);

        // 找到是哪个按钮失去焦点
        for (int i = 0; i < 3; i++) {
            if (index_menu_buttons[i] == btn) {
                ESP_LOGI(TAG, "Button %d lost FOCUS", i);
                break;
            }
        }

        // 手动刷新模式: 使失去焦点的按钮无效并触发渲染
        // DEFOCUS和FOCUS会连续触发,只需在FOCUS时刷新EPD即可
        lv_obj_invalidate(btn);
        lvgl_trigger_render(NULL);
    }
}

// 按钮按键事件处理 - 只用于日志记录，焦点导航由 lv_group 自动处理
static void index_button_key_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);
        ESP_LOGI(TAG, "Button key event: key=%u (%s)",
                 key,
                 key == LV_KEY_PREV ? "PREV (was UP)" :
                 key == LV_KEY_NEXT ? "NEXT (was DOWN)" :
                 key == LV_KEY_ENTER ? "ENTER" : "OTHER");
    }
}

static void index_menu_button_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);

    ESP_LOGI(TAG, "Button event callback: code=%d, target=%p", code, btn);

    if (btn == NULL) {
        return;
    }

    // 从用户数据中获取按钮索引
    const uintptr_t btn_index = (uintptr_t)lv_obj_get_user_data(btn);
    ESP_LOGI(TAG, "Button index from user_data: %u", btn_index);

    // 处理点击事件（由 ENTER 键或触摸触发）
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button %u clicked", btn_index);
        index_activate_menu(btn_index);
    }
}

static void index_screen_destroy_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Index screen destroyed, resetting state");
    for (int i = 0; i < 3; i++) {
        index_menu_buttons[i] = NULL;
    }
    s_index_group = NULL;
    s_last_focused_button = NULL;
}

void index_screen_create(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str, lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Creating Monster For Pan menu screen");

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 注册屏幕销毁回调
    lv_obj_add_event_cb(screen, index_screen_destroy_cb, LV_EVENT_DELETE, NULL);

    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // ========================================
    // 第1部分: 顶部标题区域
    // ========================================
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Monster For Pan");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "ESP32-C3-X4 System");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_black(), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *line_top = lv_line_create(screen);
    static lv_point_precise_t line_top_points[] = {{10, 70}, {470, 70}};
    lv_line_set_points(line_top, line_top_points, 2);
    lv_obj_set_style_line_width(line_top, 2, 0);
    lv_obj_set_style_line_color(line_top, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_top, LV_OPA_COVER, 0);

    // ========================================
    // 第2部分: 系统信息区域
    // ========================================
    lv_obj_t *info_label = lv_label_create(screen);
    lv_label_set_text(info_label, "System Info:");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_label, lv_color_black(), 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 20, 85);

    // 电池信息
    char bat_str[64];
    snprintf(bat_str, sizeof(bat_str), "Battery: %" PRIu32 " mV (%u%%)", battery_mv, battery_pct);
    lv_obj_t *bat_label = lv_label_create(screen);
    lv_label_set_text(bat_label, bat_str);
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_label, lv_color_black(), 0);
    lv_obj_align(bat_label, LV_ALIGN_TOP_LEFT, 20, 108);

    // 充电状态
    lv_obj_t *status_label = lv_label_create(screen);
    lv_label_set_text(status_label, charging ? "Status: Charging" : "Status: On Battery");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 20, 128);

    // ========================================
    // 第3部分: 菜单选择区域
    // ========================================
    lv_obj_t *line_menu = lv_line_create(screen);
    static lv_point_precise_t line_menu_points[] = {{10, 158}, {470, 158}};
    lv_line_set_points(line_menu, line_menu_points, 2);
    lv_obj_set_style_line_width(line_menu, 1, 0);
    lv_obj_set_style_line_color(line_menu, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_menu, LV_OPA_COVER, 0);

    lv_obj_t *menu_title = lv_label_create(screen);
    lv_label_set_text(menu_title, "Main Menu:");
    lv_obj_set_style_text_font(menu_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(menu_title, lv_color_black(), 0);
    lv_obj_align(menu_title, LV_ALIGN_TOP_LEFT, 20, 170);

    // ========================================
    // 菜单按钮 - 使用独立的 lv_button 控件
    // ========================================
    const char *button_texts[3] = {
        "1. SDCard File Browser",
        "2. BLE Reader",
        "3. Settings"
    };

    // 创建容器用于放置按钮（使用 flex 垂直布局）
    lv_obj_t *menu_container = lv_obj_create(screen);
    lv_obj_set_size(menu_container, 440, 200);
    lv_obj_align(menu_container, LV_ALIGN_TOP_LEFT, 20, 200);
    lv_obj_set_layout(menu_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(menu_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menu_container, 10, 0);
    lv_obj_set_style_pad_column(menu_container, 0, 0);
    lv_obj_set_style_pad_all(menu_container, 0, 0);
    lv_obj_set_style_border_width(menu_container, 0, 0);
    lv_obj_set_style_bg_opa(menu_container, LV_OPA_TRANSP, 0);

    // 创建三个独立的按钮
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(menu_container);
        lv_obj_set_size(btn, 440, 60);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);  // 保存按钮索引

        // 极简按钮样式 - 适合EPD黑白显示
        // 默认状态: 白色背景,细边框
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);  // 禁用渐变
        lv_obj_set_style_border_color(btn, lv_color_black(), 0);
        lv_obj_set_style_border_width(btn, 1, 0);  // 细边框
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_radius(btn, 0, 0);            // 无圆角
        lv_obj_set_style_shadow_width(btn, 0, 0);  // 无阴影
        lv_obj_set_style_pad_all(btn, 10, 0);      // 内边距
        
        // 不设置焦点状态样式,使用LVGL默认焦点效果
        lv_obj_set_style_border_width(btn, 3, LV_STATE_FOCUSED);  // 焦点时加粗边框

        // 按钮标签
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, button_texts[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);  // 默认黑字
        lv_obj_center(label);

        // 添加事件回调
        // 监听 KEY 事件，处理 UP/DOWN 焦点切换和 ENTER 键
        lv_obj_add_event_cb(btn, index_button_key_event_cb, LV_EVENT_KEY, NULL);
        // 监听 FOCUSED 事件，当焦点切换到这个按钮时触发 EPD 刷新
        lv_obj_add_event_cb(btn, index_button_focus_event_cb, LV_EVENT_FOCUSED, NULL);
        // 监听 DEFOCUSED 事件，当按钮失去焦点时使其无效
        lv_obj_add_event_cb(btn, index_button_focus_event_cb, LV_EVENT_DEFOCUSED, NULL);
        // 监听 CLICKED 事件（由 ENTER 键触发）
        lv_obj_add_event_cb(btn, index_menu_button_event_cb, LV_EVENT_CLICKED, NULL);

        // 保存按钮指针
        index_menu_buttons[i] = btn;

        ESP_LOGI(TAG, "Created menu button %d: %s", i, button_texts[i]);
    }

    // 设置默认焦点到第一个按钮
    if (index_menu_buttons[0] != NULL) {
        lv_group_focus_obj(index_menu_buttons[0]);
        s_last_focused_button = index_menu_buttons[0];  // 记录初始焦点
        ESP_LOGI(TAG, "Set initial focus to button 0");
    }

    // ========================================
    // 第4部分: 底部操作提示
    // ========================================
    lv_obj_t *line_bottom = lv_line_create(screen);
    static lv_point_precise_t line_bottom_points[] = {{10, 720}, {470, 720}};
    lv_line_set_points(line_bottom, line_bottom_points, 2);
    lv_obj_set_style_line_width(line_bottom, 2, 0);
    lv_obj_set_style_line_color(line_bottom, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_bottom, LV_OPA_COVER, 0);

    lv_obj_t *hint1 = lv_label_create(screen);
    lv_label_set_text(hint1, "Vol+/-: Select menu");
    lv_obj_set_style_text_font(hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint1, lv_color_black(), 0);
    lv_obj_align(hint1, LV_ALIGN_TOP_LEFT, 20, 730);

    lv_obj_t *hint2 = lv_label_create(screen);
    lv_label_set_text(hint2, "Confirm(3): Enter");
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint2, lv_color_black(), 0);
    lv_obj_align(hint2, LV_ALIGN_TOP_LEFT, 20, 750);

    lv_obj_t *hint3 = lv_label_create(screen);
    lv_label_set_text(hint3, "Back(4): Return");
    lv_obj_set_style_text_font(hint3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint3, lv_color_black(), 0);
    lv_obj_align(hint3, LV_ALIGN_TOP_LEFT, 20, 770);

    // 版本信息
    if (version_str && strlen(version_str) > 0) {
        lv_obj_t *version_label = lv_label_create(screen);
        lv_label_set_text(version_label, version_str);
        lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(version_label, lv_color_black(), 0);
        lv_obj_align(version_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    // 设置焦点（使用 LVGL group，启用循环导航）
    // 事件回调已在按钮创建时添加
    if (indev != NULL) {
        s_index_group = lv_group_create();

        // 配置 group 属性
        lv_group_set_wrap(s_index_group, true);  // 启用循环导航

        // 将所有三个按钮都添加到同一个 group
        for (int i = 0; i < 3; i++) {
            if (index_menu_buttons[i] != NULL) {
                lv_group_add_obj(s_index_group, index_menu_buttons[i]);
                ESP_LOGI(TAG, "Added button %d to group", i);
            }
        }

        // 将 group 绑定到输入设备
        lv_indev_set_group(indev, s_index_group);

        // 验证 group 配置
        lv_obj_t *focused_obj = lv_group_get_focused(s_index_group);
        ESP_LOGI(TAG, "Group setup complete. Focused object: %p (expected button 0: %p)",
                 focused_obj, index_menu_buttons[0]);
        ESP_LOGI(TAG, "Group size: %d, wrap: %d",
                 lv_group_get_obj_count(s_index_group), lv_group_get_wrap(s_index_group));
    }

    // 强制 LVGL 完成初始渲染（手动刷新模式）
    // 先使屏幕无效，确保 LVGL 重新渲染所有内容
    lv_obj_invalidate(screen);

    // 使用手动刷新模式：触发渲染
    for (int i = 0; i < 5; i++) {
        lvgl_trigger_render(NULL);  // 触发 LVGL 渲染
    }

    // 初始刷新 EPD - 由 screen_manager 设置刷新模式（组件间切换用 FULL）
    lvgl_display_refresh();

    ESP_LOGI(TAG, "Monster For Pan menu screen created successfully");
}
