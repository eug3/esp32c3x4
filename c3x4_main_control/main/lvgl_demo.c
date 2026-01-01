/**
 * @file lvgl_demo.c
 * @brief LVGL示例GUI应用 - 展示各种UI元素
 */

#include "lvgl_demo.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LVGL_DEMO";

// UI对象 (仅欢迎页使用)
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

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_DEFOCUSED) {
        welcome_schedule_epd_refresh(250);
        return;
    }

    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);

        if (key == LV_KEY_PREV || key == LV_KEY_UP) {
            if (welcome_menu_selected > 0) {
                welcome_btnm_set_selected(btnm, welcome_menu_selected - 1);
                welcome_schedule_epd_refresh(250);
            }
            return;
        }

        if (key == LV_KEY_NEXT || key == LV_KEY_DOWN) {
            if (welcome_menu_selected < 1) {
                welcome_btnm_set_selected(btnm, welcome_menu_selected + 1);
                welcome_schedule_epd_refresh(250);
            }
            return;
        }

        if (key == LV_KEY_ENTER) {
            welcome_activate_menu(welcome_menu_selected);
            welcome_schedule_epd_refresh(250);
            return;
        }
    }

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

// 创建主屏幕
void lvgl_demo_create_main_screen(void)
{
    ESP_LOGI(TAG, "Creating main screen");
    
    // 创建主屏幕
    main_screen = lv_obj_create(NULL);
    lv_scr_load(main_screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(main_screen, lv_color_white(), 0);
    
    // 创建标题标签
    lv_obj_t *label_title = lv_label_create(main_screen);
    lv_label_set_text(label_title, "Xteink X4 - LVGL Demo");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 创建信息标签
    lv_obj_t *label_info = lv_label_create(main_screen);
    lv_label_set_text(label_info, "Press buttons to interact");
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, 0);
    lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 60);
    
    // 创建按钮
    lv_obj_t *btn_menu = lv_btn_create(main_screen);
    lv_obj_set_size(btn_menu, 200, 60);
    lv_obj_align(btn_menu, LV_ALIGN_CENTER, 0, -50);
    
    lv_obj_t *btn_label = lv_label_create(btn_menu);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);
    
    ESP_LOGI(TAG, "Main screen created");
}

// 创建菜单屏幕
void lvgl_demo_create_menu_screen(void)
{
    ESP_LOGI(TAG, "Creating menu screen");
    
    // 创建主屏幕
    main_screen = lv_obj_create(NULL);
    lv_scr_load(main_screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(main_screen, lv_color_white(), 0);
    
    // 创建标题
    lv_obj_t *label_title = lv_label_create(main_screen);
    lv_label_set_text(label_title, "Main Menu");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建信息标签
    lv_obj_t *label_info = lv_label_create(main_screen);
    lv_label_set_text(label_info, "Use UP/DOWN to navigate");
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, 0);
    lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 45);
    
    // 创建列表
    lv_obj_t *list_menu = lv_list_create(main_screen);
    lv_obj_set_size(list_menu, 300, 350);
    lv_obj_align(list_menu, LV_ALIGN_CENTER, 0, 20);
    
    // 添加列表项
    lv_obj_t *btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "Settings");
    (void)btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "File Browser");
    (void)btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "Network");
    (void)btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "Battery Info");
    (void)btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "About");
    (void)btn;
    
    ESP_LOGI(TAG, "Menu screen created");
}

// 创建信息显示屏幕
void lvgl_demo_create_info_screen(const char *title, const char *info_text)
{
    ESP_LOGI(TAG, "Creating info screen");
    
    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 创建标题
    lv_obj_t *label_t = lv_label_create(screen);
    lv_label_set_text(label_t, title);
    lv_obj_set_style_text_font(label_t, &lv_font_montserrat_14, 0);
    lv_obj_align(label_t, LV_ALIGN_TOP_MID, 0, 20);
    
    // 创建文本区域
    lv_obj_t *textarea = lv_textarea_create(screen);
    lv_obj_set_size(textarea, 700, 380);
    lv_obj_align(textarea, LV_ALIGN_CENTER, 0, 20);
    lv_textarea_set_text(textarea, info_text);
    
    // 返回按钮提示
    lv_obj_t *label_hint = lv_label_create(screen);
    lv_label_set_text(label_hint, "Press BACK to return");
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(label_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// 创建进度条示例
void lvgl_demo_create_progress_screen(void)
{
    ESP_LOGI(TAG, "Creating progress screen");
    
    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 标题
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Progress Example");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    
    // 进度条1
    lv_obj_t *bar1 = lv_bar_create(screen);
    lv_obj_set_size(bar1, 400, 30);
    lv_obj_align(bar1, LV_ALIGN_CENTER, 0, -80);
    lv_bar_set_value(bar1, 35, LV_ANIM_OFF);
    
    lv_obj_t *bar1_label = lv_label_create(screen);
    lv_label_set_text(bar1_label, "Battery: 35%");
    lv_obj_align_to(bar1_label, bar1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // 进度条2
    lv_obj_t *bar2 = lv_bar_create(screen);
    lv_obj_set_size(bar2, 400, 30);
    lv_obj_align(bar2, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_value(bar2, 75, LV_ANIM_OFF);
    
    lv_obj_t *bar2_label = lv_label_create(screen);
    lv_label_set_text(bar2_label, "Storage: 75%");
    lv_obj_align_to(bar2_label, bar2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // 滑块
    lv_obj_t *slider = lv_slider_create(screen);
    lv_obj_set_size(slider, 400, 20);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 80);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    
    lv_obj_t *slider_label = lv_label_create(screen);
    lv_label_set_text(slider_label, "Brightness: 50%");
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

// 创建简单的启动画面
void lvgl_demo_create_splash_screen(void)
{
    ESP_LOGI(TAG, "Creating splash screen");
    
    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 大标题
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Xteink X4");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);
    
    // 副标题
    lv_obj_t *sublabel = lv_label_create(screen);
    lv_label_set_text(sublabel, "E-Ink Device");
    lv_obj_set_style_text_font(sublabel, &lv_font_montserrat_14, 0);
    lv_obj_align(sublabel, LV_ALIGN_CENTER, 0, 0);
    
    // 版本信息
    lv_obj_t *version = lv_label_create(screen);
    lv_label_set_text(version, "LVGL GUI v1.0");
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// 创建完整的欢迎屏幕（替换 display_welcome_screen）
void lvgl_demo_create_welcome_screen(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str)
{
    ESP_LOGI(TAG, "Creating welcome screen with system info");
    
    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    
    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 极简：只保留一个标题 + 菜单，避免过多控件和装饰图形（节省内存/刷新开销）。
    (void)battery_mv;
    (void)battery_pct;
    (void)charging;
    (void)version_str;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Monster C3X4");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    
    // Menu: use btnmatrix so there are NO per-item label objects to style.
    // (Text is rendered by the widget itself; selection highlight uses LV_STATE_CHECKED.)
    static const char *welcome_btnm_map[] = {
        "1. File Browser", "\n",
        "2. BLE Reader", ""
    };

    welcome_menu_btnm = lv_btnmatrix_create(screen);
    lv_btnmatrix_set_map(welcome_menu_btnm, welcome_btnm_map);
    lv_obj_set_size(welcome_menu_btnm, 440, 220);
    lv_obj_align(welcome_menu_btnm, LV_ALIGN_TOP_LEFT, 20, 60);

    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_white(), 0);
    lv_obj_set_style_border_width(welcome_menu_btnm, 1, 0);
    lv_obj_set_style_border_color(welcome_menu_btnm, lv_color_black(), 0);
    lv_obj_set_style_pad_all(welcome_menu_btnm, 6, 0);
    lv_obj_set_style_pad_row(welcome_menu_btnm, 12, 0);
    lv_obj_set_style_pad_column(welcome_menu_btnm, 12, 0);

    // Items (buttons)
    lv_obj_set_style_text_font(welcome_menu_btnm, &lv_font_montserrat_14, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(welcome_menu_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(welcome_menu_btnm, 1, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_DEFAULT);

    // Selected/checked item: black background + white text + thicker border
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(welcome_menu_btnm, 3, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_CHECKED);

    // Make buttons checkable and keep only one checked
    lv_btnmatrix_set_btn_ctrl_all(welcome_menu_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(welcome_menu_btnm, true);

    // Initial selection
    welcome_menu_selected = 0;
    lv_btnmatrix_set_selected_btn(welcome_menu_btnm, welcome_menu_selected);
    lv_btnmatrix_set_btn_ctrl(welcome_menu_btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);
    
    // ========================================
    // 创建输入组，使按键可以控制菜单
    // ========================================
    lv_group_t *group = lv_group_create();
    lv_group_add_obj(group, welcome_menu_btnm);
    lv_obj_add_event_cb(welcome_menu_btnm, welcome_menu_btnm_event_cb, LV_EVENT_ALL, NULL);

    // Ensure there is an initial focused object so keys work immediately.
    lv_group_focus_obj(welcome_menu_btnm);
    
    // Initialize refresh timer after setting initial focus to avoid spurious refresh
    welcome_last_epd_refresh_ms = lv_tick_get();
    
    // 获取输入设备并设置组
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_set_group(indev, group);
        ESP_LOGI(TAG, "Input device group set for welcome screen");
    }
    
    // 极简提示：只保留一行
    lv_obj_t *hint = lv_label_create(screen);
    lv_label_set_text(hint, "UP/DOWN: Select   ENTER: OK");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    ESP_LOGI(TAG, "Welcome screen created successfully");
}
