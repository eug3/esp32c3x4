/**
 * @file lvgl_demo.c
 * @brief LVGL GUI - 主菜单屏幕（Monster For Pan）
 */

#include "lvgl_demo.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LVGL_DEMO";

// UI对象
static lv_obj_t *main_screen = NULL;

// Welcome screen helpers (EPD refresh scheduling)
static lv_timer_t *welcome_refresh_timer = NULL;
static uint32_t welcome_last_epd_refresh_ms = 0;

// Welcome menu (use btnmatrix to avoid per-child label focus styling)
static lv_obj_t *welcome_menu_btnm = NULL;
static uint16_t welcome_menu_selected = 0;

static void welcome_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t now = lv_tick_get();
    const uint32_t min_interval_ms = 800; // partial refresh is cheaper; still avoid flicker

    if (welcome_last_epd_refresh_ms != 0 && (now - welcome_last_epd_refresh_ms) < min_interval_ms) {
        const uint32_t remain = min_interval_ms - (now - welcome_last_epd_refresh_ms);
        lv_timer_set_period(welcome_refresh_timer, remain + 50);
        lv_timer_reset(welcome_refresh_timer);
        return;
    }

    lvgl_display_refresh();
    welcome_last_epd_refresh_ms = now;

    // one-shot behavior
    lv_timer_pause(welcome_refresh_timer);
}

static void welcome_schedule_epd_refresh(uint32_t delay_ms)
{
    if (welcome_refresh_timer == NULL) {
        welcome_refresh_timer = lv_timer_create(welcome_refresh_timer_cb, delay_ms, NULL);
        lv_timer_pause(welcome_refresh_timer);
        lv_timer_set_repeat_count(welcome_refresh_timer, -1);
    }

    lv_timer_set_period(welcome_refresh_timer, delay_ms);
    lv_timer_reset(welcome_refresh_timer);
    lv_timer_resume(welcome_refresh_timer);
}

static void welcome_activate_menu(uint16_t menu_index)
{
    ESP_LOGI(TAG, "Welcome menu activated: %u", (unsigned)menu_index);
}

static void welcome_btnm_set_selected(lv_obj_t *btnm, uint16_t new_index)
{
    if (btnm == NULL) return;
    if (new_index > 1) return;

    lv_btnmatrix_clear_btn_ctrl(btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);
    welcome_menu_selected = new_index;
    lv_btnmatrix_set_selected_btn(btnm, welcome_menu_selected);
    lv_btnmatrix_set_btn_ctrl(btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);
}

static void welcome_menu_btnm_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btnm = lv_event_get_target(e);

    // FOCUSED/DEFOCUSED 事件：触发 EPD 刷新
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_DEFOCUSED) {
        welcome_schedule_epd_refresh(250);
        return;
    }

    // KEY 事件：处理 PREV/NEXT/ENTER 键
    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);
        ESP_LOGI(TAG, "KEY event received: key=%u", (unsigned)key);

        if (key == LV_KEY_PREV || key == LV_KEY_UP) {
            if (welcome_menu_selected > 0) {
                welcome_btnm_set_selected(btnm, welcome_menu_selected - 1);
                welcome_schedule_epd_refresh(250);
            } else {
                ESP_LOGI(TAG, "Already at first item");
            }
            return;
        }

        if (key == LV_KEY_NEXT || key == LV_KEY_DOWN) {
            if (welcome_menu_selected < 1) {
                welcome_btnm_set_selected(btnm, welcome_menu_selected + 1);
                welcome_schedule_epd_refresh(250);
            } else {
                ESP_LOGI(TAG, "Already at last item");
            }
            return;
        }

        if (key == LV_KEY_ENTER) {
            welcome_activate_menu(welcome_menu_selected);
            welcome_schedule_epd_refresh(250);
            return;
        }
    }

    // 处理点击事件
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_CLICKED) {
        const uint16_t sel = lv_btnmatrix_get_selected_btn(btnm);
        if (sel <= 1) {
            welcome_btnm_set_selected(btnm, sel);
        }
        welcome_activate_menu(welcome_menu_selected);
        welcome_schedule_epd_refresh(250);
        return;
    }
}

// 创建主屏幕（Monster For Pan 菜单）
void lvgl_demo_create_welcome_screen(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str)
{
    ESP_LOGI(TAG, "Creating Monster For Pan menu screen");

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 设置背景为黑色
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // ========================================
    // 第1部分: 顶部标题区域
    // ========================================
    // 主标题 "Monster For Pan"
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Monster For Pan");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // 副标题 "ESP32-C3 System"
    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "ESP32-C3 System");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_white(), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 40);

    // 顶部分隔线
    lv_obj_t *line_top = lv_line_create(screen);
    static lv_point_precise_t line_top_points[] = {{10, 70}, {470, 70}};
    lv_line_set_points(line_top, line_top_points, 2);
    lv_obj_set_style_line_width(line_top, 2, 0);
    lv_obj_set_style_line_color(line_top, lv_color_white(), 0);
    lv_obj_set_style_line_opa(line_top, LV_OPA_COVER, 0);

    // ========================================
    // 第2部分: 系统信息区域
    // ========================================
    lv_obj_t *info_label = lv_label_create(screen);
    lv_label_set_text(info_label, "System Info:");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 20, 85);

    // 电池信息
    char bat_str[64];
    snprintf(bat_str, sizeof(bat_str), "Battery: %lu mV (%u%%)", battery_mv, battery_pct);
    lv_obj_t *bat_label = lv_label_create(screen);
    lv_label_set_text(bat_label, bat_str);
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_label, lv_color_white(), 0);
    lv_obj_align(bat_label, LV_ALIGN_TOP_LEFT, 20, 108);

    // 充电状态
    lv_obj_t *status_label = lv_label_create(screen);
    lv_label_set_text(status_label, charging ? "Status: Charging" : "Status: On Battery");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 20, 128);

    // ========================================
    // 第3部分: 菜单选择区域
    // ========================================
    // 中部分隔线
    lv_obj_t *line_menu = lv_line_create(screen);
    static lv_point_precise_t line_menu_points[] = {{10, 158}, {470, 158}};
    lv_line_set_points(line_menu, line_menu_points, 2);
    lv_obj_set_style_line_width(line_menu, 1, 0);
    lv_obj_set_style_line_color(line_menu, lv_color_white(), 0);
    lv_obj_set_style_line_opa(line_menu, LV_OPA_COVER, 0);

    lv_obj_t *menu_title = lv_label_create(screen);
    lv_label_set_text(menu_title, "Main Menu:");
    lv_obj_set_style_text_font(menu_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(menu_title, lv_color_white(), 0);
    lv_obj_align(menu_title, LV_ALIGN_TOP_LEFT, 20, 170);

    // Menu: use btnmatrix so there are NO per-item label objects to style.
    static const char *welcome_btnm_map[] = {
        "1. File Browser", "\n",
        "2. BLE Reader", ""
    };

    welcome_menu_btnm = lv_btnmatrix_create(screen);
    lv_btnmatrix_set_map(welcome_menu_btnm, welcome_btnm_map);
    lv_obj_set_size(welcome_menu_btnm, 420, 140);
    lv_obj_align(welcome_menu_btnm, LV_ALIGN_TOP_LEFT, 30, 200);

    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(welcome_menu_btnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(welcome_menu_btnm, 0, 0);
    lv_obj_set_style_pad_all(welcome_menu_btnm, 6, 0);
    lv_obj_set_style_pad_row(welcome_menu_btnm, 12, 0);
    lv_obj_set_style_pad_column(welcome_menu_btnm, 12, 0);

    // Items (buttons) - default state (黑底白字)
    lv_obj_set_style_text_font(welcome_menu_btnm, &lv_font_montserrat_14, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(welcome_menu_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(welcome_menu_btnm, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(welcome_menu_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(welcome_menu_btnm, 6, LV_PART_ITEMS | LV_STATE_DEFAULT);

    // Selected/checked item: 白底黑字 + thicker border (反转)
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(welcome_menu_btnm, 3, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(welcome_menu_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);

    // Make buttons checkable and keep only one checked
    lv_btnmatrix_set_btn_ctrl_all(welcome_menu_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(welcome_menu_btnm, true);

    // Initial selection
    welcome_menu_selected = 0;
    lv_btnmatrix_set_selected_btn(welcome_menu_btnm, welcome_menu_selected);
    lv_btnmatrix_set_btn_ctrl(welcome_menu_btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);

    // ========================================
    // 第4部分: 底部操作提示
    // ========================================
    lv_obj_t *line_bottom = lv_line_create(screen);
    static lv_point_precise_t line_bottom_points[] = {{10, 720}, {470, 720}};
    lv_line_set_points(line_bottom, line_bottom_points, 2);
    lv_obj_set_style_line_width(line_bottom, 2, 0);
    lv_obj_set_style_line_color(line_bottom, lv_color_white(), 0);
    lv_obj_set_style_line_opa(line_bottom, LV_OPA_COVER, 0);

    // 操作提示
    lv_obj_t *hint1 = lv_label_create(screen);
    lv_label_set_text(hint1, "UP/DOWN: Select");
    lv_obj_set_style_text_font(hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint1, lv_color_white(), 0);
    lv_obj_align(hint1, LV_ALIGN_TOP_LEFT, 20, 730);

    lv_obj_t *hint2 = lv_label_create(screen);
    lv_label_set_text(hint2, "CONFIRM: Enter");
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint2, lv_color_white(), 0);
    lv_obj_align(hint2, LV_ALIGN_TOP_LEFT, 20, 750);

    lv_obj_t *hint3 = lv_label_create(screen);
    lv_label_set_text(hint3, "POWER: Sleep (1s)");
    lv_obj_set_style_text_font(hint3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint3, lv_color_white(), 0);
    lv_obj_align(hint3, LV_ALIGN_TOP_LEFT, 20, 770);

    // 版本信息 (右下角)
    if (version_str && strlen(version_str) > 0) {
        lv_obj_t *version_label = lv_label_create(screen);
        lv_label_set_text(version_label, version_str);
        lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(version_label, lv_color_white(), 0);
        lv_obj_align(version_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    // ========================================
    // 按键处理：不使用 group，直接在对象上处理 KEY 事件
    // ========================================
    // 添加事件回调到 btnmatrix
    lv_obj_add_event_cb(welcome_menu_btnm, welcome_menu_btnm_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "Monster For Pan menu screen created successfully");
}
