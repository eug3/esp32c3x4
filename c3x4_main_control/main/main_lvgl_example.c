/**
 * @file main_lvgl_example.c
 * @brief LVGL集成示例 - 在main.c中添加此代码
 * 
 * 这个文件展示如何在现有的main.c中集成LVGL
 */

/*
===========================================
在 main.c 顶部添加包含文件:
===========================================
*/
// #include "lvgl_driver.h"
// #include "lvgl_demo.h"

/*
===========================================
在 app_main() 函数中添加LVGL初始化代码:
===========================================
*/

void app_main_lvgl_example(void)
{
    // ... 现有的初始化代码 ...
    
    // 初始化EPD（如果还未初始化）
    // EPD_4IN26_Init();
    // Paint_NewImage(BlackImage, EPD_4IN26_WIDTH, EPD_4IN26_HEIGHT, 270, WHITE);
    // Paint_Clear(WHITE);
    
    // ========== 初始化LVGL ==========
    ESP_LOGI("MAIN", "Initializing LVGL...");
    
    // 初始化显示驱动
    lv_disp_t *disp = lvgl_display_init();
    if (disp == NULL) {
        ESP_LOGE("MAIN", "Failed to initialize LVGL display");
        return;
    }
    
    // 初始化输入设备驱动
    lv_indev_t *indev = lvgl_input_init();
    if (indev == NULL) {
        ESP_LOGE("MAIN", "Failed to initialize LVGL input");
        return;
    }
    
    // 创建LVGL任务
    xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 5, NULL);
    xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 5, NULL);
    
    // ========== 创建UI界面 ==========
    
    // 选项1: 创建启动画面
    lvgl_demo_create_splash_screen();
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待LVGL渲染
    lvgl_display_refresh(); // 刷新EPD显示
    vTaskDelay(pdMS_TO_TICKS(3000)); // 显示3秒
    
    // 选项2: 创建主屏幕（带按钮）
    // lvgl_demo_create_main_screen();
    // vTaskDelay(pdMS_TO_TICKS(100));
    // lvgl_display_refresh();
    
    // 选项3: 创建菜单屏幕
    lvgl_demo_create_menu_screen();
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
    
    // 选项4: 创建进度条示例
    // lvgl_demo_create_progress_screen();
    // vTaskDelay(pdMS_TO_TICKS(100));
    // lvgl_display_refresh();
    
    // 选项5: 创建信息屏幕
    // lvgl_demo_create_info_screen(
    //     "System Info",
    //     "Device: Xteink X4\n"
    //     "Display: 4.26\" EPD\n"
    //     "Resolution: 800x480\n"
    //     "CPU: ESP32-C3\n"
    //     "Framework: ESP-IDF + LVGL"
    // );
    // vTaskDelay(pdMS_TO_TICKS(100));
    // lvgl_display_refresh();
    
    ESP_LOGI("MAIN", "LVGL GUI initialized successfully!");
    
    // ========== 主循环 ==========
    // LVGL的任务会在后台持续运行
    // 当按键按下时，会触发相应的事件
    
    // 示例：定期刷新显示（根据需要调整）
    while (1) {
        // 检查是否需要刷新EPD
        // 注意：EPD刷新较慢，不要太频繁
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // 可选：在这里添加自定义的业务逻辑
    }
}

/*
===========================================
按键集成说明:
===========================================

现有的按键读取函数 get_pressed_button() 已经在 lvgl_driver.c 中使用。
LVGL会自动调用它来获取按键输入。

按键映射：
- BTN_CONFIRM  -> LV_KEY_ENTER  (确认/选择)
- BTN_BACK     -> LV_KEY_ESC    (返回/取消)
- BTN_LEFT     -> LV_KEY_LEFT   (向左)
- BTN_RIGHT    -> LV_KEY_RIGHT  (向右)
- BTN_VOLUME_UP   -> LV_KEY_UP     (向上)
- BTN_VOLUME_DOWN -> LV_KEY_DOWN   (向下)

这些按键在LVGL中可以用来：
- 在列表/菜单中导航（上/下/左/右）
- 选择/确认项目（ENTER）
- 返回上一级（ESC）

===========================================
EPD刷新优化建议:
===========================================

1. 部分刷新：
   - 对于频繁更新的内容，考虑使用EPD的部分刷新功能
   - 需要修改 disp_flush_cb 来支持部分刷新

2. 刷新时机：
   - 在用户交互后才刷新（例如按键按下后）
   - 避免在定时器中频繁刷新
   - 可以设置一个"dirty"标志，仅在需要时刷新

3. 刷新策略示例：
*/

// 全局刷新标志
static bool epd_needs_refresh = false;

// 在事件回调中设置刷新标志
void my_event_handler(lv_event_t *e)
{
    // 处理事件...
    
    // 标记需要刷新
    epd_needs_refresh = true;
}

// 在主循环中检查并刷新
void main_loop_with_refresh_check(void)
{
    while (1) {
        if (epd_needs_refresh) {
            lvgl_display_refresh();
            epd_needs_refresh = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*
===========================================
自定义UI创建示例:
===========================================
*/

void create_custom_ui_example(void)
{
    // 创建一个新屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 添加标题
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "My Custom Screen");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 添加文本
    lv_obj_t *text = lv_label_create(screen);
    lv_label_set_text(text, "Hello, LVGL on E-Ink!");
    lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
    
    // 添加按钮
    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 150, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press Me");
    lv_obj_center(btn_label);
    
    // 刷新显示
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
}

/*
===========================================
调试建议:
===========================================

1. 启用LVGL日志（在 lv_conf.h 中已启用）
2. 检查内存使用：LV_MEM_SIZE 设置为 50KB
3. 如果遇到内存不足，可以增加 LV_MEM_SIZE
4. EPD刷新时间较长，要有耐心等待
5. 使用ESP_LOGx打印调试信息

===========================================
后续优化方向:
===========================================

1. 添加中文字体支持
2. 实现自定义主题以优化EPD显示效果
3. 添加动画（注意EPD刷新速度限制）
4. 集成文件浏览器
5. 添加设置页面
6. 实现电量显示
7. 集成BLE控制界面

*/
