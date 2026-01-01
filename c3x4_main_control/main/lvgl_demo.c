/**
 * @file lvgl_demo.c
 * @brief LVGL示例GUI应用 - 展示各种UI元素
 */

#include "lvgl_demo.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "LVGL_DEMO";

// UI对象
static lv_obj_t *main_screen = NULL;
static lv_obj_t *label_title = NULL;
static lv_obj_t *label_info = NULL;
static lv_obj_t *btn_menu = NULL;
static lv_obj_t *list_menu = NULL;

// 按钮事件回调
static void btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button clicked");
        lv_label_set_text(label_info, "Button was clicked!");
        
        // 触发EPD刷新
        lvgl_display_refresh();
    }
}

// 列表项事件回调
static void list_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        const char *txt = lv_list_get_btn_text(list_menu, obj);
        ESP_LOGI(TAG, "List item clicked: %s", txt);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Selected: %s", txt);
        lv_label_set_text(label_info, buf);
        
        // 触发EPD刷新
        lvgl_display_refresh();
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
    label_title = lv_label_create(main_screen);
    lv_label_set_text(label_title, "Xteink X4 - LVGL Demo");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 创建信息标签
    label_info = lv_label_create(main_screen);
    lv_label_set_text(label_info, "Press buttons to interact");
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, 0);
    lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 60);
    
    // 创建按钮
    btn_menu = lv_btn_create(main_screen);
    lv_obj_set_size(btn_menu, 200, 60);
    lv_obj_align(btn_menu, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(btn_menu, btn_event_handler, LV_EVENT_ALL, NULL);
    
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
    label_title = lv_label_create(main_screen);
    lv_label_set_text(label_title, "Main Menu");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建信息标签
    label_info = lv_label_create(main_screen);
    lv_label_set_text(label_info, "Use UP/DOWN to navigate");
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, 0);
    lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 45);
    
    // 创建列表
    list_menu = lv_list_create(main_screen);
    lv_obj_set_size(list_menu, 300, 350);
    lv_obj_align(list_menu, LV_ALIGN_CENTER, 0, 20);
    
    // 添加列表项
    lv_obj_t *btn;
    
    btn = lv_list_add_btn(list_menu, NULL, "Settings");
    lv_obj_add_event_cb(btn, list_event_handler, LV_EVENT_CLICKED, NULL);
    
    btn = lv_list_add_btn(list_menu, NULL, "File Browser");
    lv_obj_add_event_cb(btn, list_event_handler, LV_EVENT_CLICKED, NULL);
    
    btn = lv_list_add_btn(list_menu, NULL, "Network");
    lv_obj_add_event_cb(btn, list_event_handler, LV_EVENT_CLICKED, NULL);
    
    btn = lv_list_add_btn(list_menu, NULL, "Battery Info");
    lv_obj_add_event_cb(btn, list_event_handler, LV_EVENT_CLICKED, NULL);
    
    btn = lv_list_add_btn(list_menu, NULL, "About");
    lv_obj_add_event_cb(btn, list_event_handler, LV_EVENT_CLICKED, NULL);
    
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
