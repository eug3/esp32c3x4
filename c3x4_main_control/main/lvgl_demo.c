/**
 * @file lvgl_demo.c
 * @brief LVGL GUI - 主菜单屏幕（Monster For Pan）
 */

#include "lvgl_demo.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "LVGL_DEMO";

#define SDCARD_MOUNT_POINT "/sdcard"
#define MAX_FILES 100
#define MAX_PATH_LEN 256

// UI对象 (未使用，已移除以节省内存)

// Welcome screen helpers (EPD refresh scheduling)
static lv_timer_t *welcome_refresh_timer = NULL;
static uint32_t welcome_last_epd_refresh_ms = 0;

// Welcome menu (use btnmatrix to avoid per-child label focus styling)
static lv_obj_t *welcome_menu_btnm = NULL;
static uint16_t welcome_menu_selected = 0;
static lv_indev_t *welcome_indev = NULL;  // 保存输入设备用于页面切换

// 按键队列定义
#define KEY_QUEUE_SIZE 16
typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN
} key_event_type_t;

typedef struct {
    key_event_type_t events[KEY_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} key_queue_t;

static key_queue_t key_queue = {0};

// 前置声明
static void welcome_btnm_set_selected(lv_obj_t *btnm, uint16_t new_index);

// 队列操作函数
static void key_queue_init(void)
{
    memset(&key_queue, 0, sizeof(key_queue_t));
}

static bool key_queue_push(key_event_type_t event)
{
    if (key_queue.count >= KEY_QUEUE_SIZE) {
        ESP_LOGW(TAG, "Key queue full, dropping event");
        return false;
    }
    key_queue.events[key_queue.tail] = event;
    key_queue.tail = (key_queue.tail + 1) % KEY_QUEUE_SIZE;
    key_queue.count++;
    return true;
}

static key_event_type_t key_queue_pop(void)
{
    if (key_queue.count == 0) {
        return KEY_NONE;
    }
    key_event_type_t event = key_queue.events[key_queue.head];
    key_queue.head = (key_queue.head + 1) % KEY_QUEUE_SIZE;
    key_queue.count--;
    return event;
}

static int16_t key_queue_get_net_direction(void)
{
    // 计算队列中的净方向（上键为-1，下键为+1）
    // 支持多次按键，例如按了5次下键，返回+5
    int16_t net_direction = 0;
    uint8_t processed = 0;

    while (key_queue.count > 0 && processed < KEY_QUEUE_SIZE) {
        key_event_type_t event = key_queue_pop();
        if (event == KEY_UP) {
            net_direction--;
        } else if (event == KEY_DOWN) {
            net_direction++;
        }
        processed++;
    }

    return net_direction;
}

static void welcome_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;

    // 处理队列中的按键
    int16_t net_direction = key_queue_get_net_direction();

    if (net_direction != 0 && welcome_menu_btnm != NULL) {
        // 根据净方向计算新的选中项
        int16_t new_index = ((int16_t)welcome_menu_selected + net_direction) % 3;

        // 处理负数的情况
        if (new_index < 0) {
            new_index += 3;
        }

        ESP_LOGI(TAG, "Timer callback: processing queued keys net_direction=%d, %d -> %d",
                 net_direction, welcome_menu_selected, new_index);

        // 应用新的选中项
        welcome_btnm_set_selected(welcome_menu_btnm, (uint16_t)new_index);

        // 关键：强制 LVGL 立即处理所有待渲染的内容
        for (int i = 0; i < 5; i++) {
            lv_timer_handler();
        }

        // 立即开始下一轮刷新
        welcome_last_epd_refresh_ms = lv_tick_get();
        lvgl_set_refresh_mode(EPD_REFRESH_FAST);
        lvgl_display_refresh();
    } else {
        ESP_LOGI(TAG, "Timer callback: no queued keys to process");
    }

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

// 处理按键事件的核心逻辑
static void welcome_process_key(key_event_type_t key_type)
{
    const uint32_t now = lv_tick_get();
    const uint32_t min_interval_ms = 800;

    // 检查距离上次刷新时间是否太短
    if (welcome_last_epd_refresh_ms != 0 && (now - welcome_last_epd_refresh_ms) < min_interval_ms) {
        // 距离上次刷新太近，将按键加入队列
        ESP_LOGI(TAG, "Too soon after last refresh, queuing key (queue count: %u)", key_queue.count);
        key_queue_push(key_type);

        // 等待刷新完成后再处理
        const uint32_t remain = min_interval_ms - (now - welcome_last_epd_refresh_ms);
        welcome_schedule_epd_refresh(remain + 50);
        return;
    }

    // 可以立即处理
    ESP_LOGI(TAG, "Processing key immediately");

    // 先处理队列中的所有按键
    int16_t net_direction = key_queue_get_net_direction();

    // 加上当前按键
    if (key_type == KEY_UP) {
        net_direction--;
    } else if (key_type == KEY_DOWN) {
        net_direction++;
    }

    // 计算新位置
    if (net_direction != 0 && welcome_menu_btnm != NULL) {
        int16_t new_index = ((int16_t)welcome_menu_selected + net_direction) % 3;
        if (new_index < 0) {
            new_index += 3;
        }

        ESP_LOGI(TAG, "Processing key: net_direction=%d, %d -> %d",
                 net_direction, welcome_menu_selected, new_index);

        // 应用新的选中项
        welcome_btnm_set_selected(welcome_menu_btnm, (uint16_t)new_index);

        // 关键：强制 LVGL 立即处理所有待渲染的内容
        // 通过多次调用 lv_timer_handler() 来确保所有 invalidate 都被处理
        for (int i = 0; i < 5; i++) {
            lv_timer_handler();
        }

        // 更新时间戳
        welcome_last_epd_refresh_ms = lv_tick_get();

        // 设置刷新模式并触发刷新
        lvgl_set_refresh_mode(EPD_REFRESH_FAST);
        lvgl_display_refresh();

        // 调度回调来处理后续可能入队的按键
        welcome_schedule_epd_refresh(min_interval_ms);
    }
}

static void welcome_activate_menu(uint16_t menu_index)
{
    ESP_LOGI(TAG, "Welcome menu activated: %u", (unsigned)menu_index);

    // 根据菜单索引执行相应操作
    switch (menu_index) {
        case 0:  // SDCard File Browser
            ESP_LOGI(TAG, "Launching SD Card File Browser...");
            lvgl_demo_create_file_browser_screen(welcome_indev);
            break;

        case 1:  // BLE Reader
            ESP_LOGI(TAG, "BLE Reader selected (not implemented yet)");
            // TODO: 实现 BLE Reader 功能
            break;

        case 2:  // Settings
            ESP_LOGI(TAG, "Settings selected (not implemented yet)");
            // TODO: 实现设置功能
            break;

        default:
            ESP_LOGW(TAG, "Unknown menu index: %u", menu_index);
            break;
    }
}

static void welcome_btnm_set_selected(lv_obj_t *btnm, uint16_t new_index)
{
    // 直接使用全局变量，忽略传入的 btnm 参数（避免事件目标对象不正确的问题）
    if (welcome_menu_btnm == NULL) {
        ESP_LOGW(TAG, "ERROR: welcome_menu_btnm is NULL, cannot set selection");
        return;
    }

    if (new_index > 2) {
        ESP_LOGW(TAG, "ERROR: new_index=%u exceeds menu size (0-2)", new_index);
        return;
    }

    uint16_t old_index = welcome_menu_selected;

    ESP_LOGI(TAG, "Changing menu selection: %u -> %u", old_index, new_index);

    // 调试：检查修改前的状态
    bool old_checked = lv_btnmatrix_has_button_ctrl(welcome_menu_btnm, old_index, LV_BTNMATRIX_CTRL_CHECKED);
    bool new_checked_before = lv_btnmatrix_has_button_ctrl(welcome_menu_btnm, new_index, LV_BTNMATRIX_CTRL_CHECKED);
    ESP_LOGI(TAG, "Before: button %u CHECKED=%d, button %u CHECKED=%d",
             old_index, old_checked, new_index, new_checked_before);

    // 更新全局状态
    welcome_menu_selected = new_index;

    // 在 LVGL 按钮矩阵中，换行符 "\n" 不占用按钮索引
    // 按钮索引是按照实际按钮连续编号的：0, 1, 2

    // 方法：先清除旧的 CHECKED，再设置新的
    if (old_index != new_index) {
        lv_btnmatrix_clear_btn_ctrl(welcome_menu_btnm, old_index, LV_BTNMATRIX_CTRL_CHECKED);
    }
    lv_btnmatrix_set_btn_ctrl(welcome_menu_btnm, new_index, LV_BTNMATRIX_CTRL_CHECKED);

    // 同时更新 selected_btn（用于键盘导航）
    lv_btnmatrix_set_selected_btn(welcome_menu_btnm, new_index);

    // 调试：检查修改后的状态
    bool old_checked_after = lv_btnmatrix_has_button_ctrl(welcome_menu_btnm, old_index, LV_BTNMATRIX_CTRL_CHECKED);
    bool new_checked_after = lv_btnmatrix_has_button_ctrl(welcome_menu_btnm, new_index, LV_BTNMATRIX_CTRL_CHECKED);
    uint16_t selected_btn = lv_btnmatrix_get_selected_btn(welcome_menu_btnm);
    ESP_LOGI(TAG, "After: button %u CHECKED=%d, button %u CHECKED=%d, selected_btn=%u",
             old_index, old_checked_after, new_index, new_checked_after, selected_btn);

    // 强制标记按钮矩阵需要重新计算样式和布局
    // 使用多个 API 确保样式被重新计算
    lv_obj_mark_layout_as_dirty(welcome_menu_btnm);
    lv_obj_invalidate(welcome_menu_btnm);

    // 强制所有子项（按钮）也标记为无效
    for (uint16_t i = 0; i < 3; i++) {
        // 注意：在 LVGL 9.x 中，我们直接无效整个按钮矩阵
        // 这里调用额外的 invalidate 确保每个按钮都被重绘
    }

    ESP_LOGI(TAG, "Menu selection changed: %u -> %u (INVALIDATED & LAYOUT DIRTY)", old_index, new_index);
}

static void welcome_menu_btnm_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btnm = lv_event_get_target(e);

    // 检查btnm有效性
    if (btnm == NULL) {
        ESP_LOGW(TAG, "Event callback: btnm is NULL");
        return;
    }

    // FOCUSED/DEFOCUSED 事件：触发 EPD 刷新
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_DEFOCUSED) {
        welcome_schedule_epd_refresh(250);
        return;
    }

    // KEY 事件：处理 UP/DOWN/ENTER 键
    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);

        ESP_LOGI(TAG, "KEY event received: key=%u, last_refresh=%lu, now=%lu",
                 (unsigned)key, welcome_last_epd_refresh_ms, lv_tick_get());

        // 检查key有效性
        if (key == 0) {
            ESP_LOGW(TAG, "Received invalid key (0)");
            return;
        }

        // 向上选择（按键5：音量+）
        if (key == LV_KEY_UP) {
            ESP_LOGI(TAG, "Processing UP key");
            welcome_process_key(KEY_UP);
            return;
        }

        // 向下选择（按键6：音量-）
        if (key == LV_KEY_DOWN) {
            ESP_LOGI(TAG, "Processing DOWN key");
            welcome_process_key(KEY_DOWN);
            return;
        }

        // 确认键（按键3）
        if (key == LV_KEY_ENTER) {
            ESP_LOGI(TAG, "Key ENTER: activate menu item %d", welcome_menu_selected);
            welcome_activate_menu(welcome_menu_selected);
            welcome_schedule_epd_refresh(250);
            return;
        }

        // 返回键（按键4）
        if (key == LV_KEY_ESC) {
            ESP_LOGI(TAG, "Key ESC/BACK pressed");
            welcome_schedule_epd_refresh(250);
            return;
        }

        // 未知的KEY事件
        ESP_LOGW(TAG, "Unknown key event: %u", (unsigned)key);
        return;
    }

    // 处理点击事件
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_CLICKED) {
        const uint16_t sel = lv_btnmatrix_get_selected_btn(btnm);
        
        // 添加范围检查
        if (sel > 2) {
            ESP_LOGW(TAG, "Invalid button selection: %u (expected 0-2)", sel);
            return;
        }
        
        ESP_LOGD(TAG, "Button selection event: sel=%u", sel);
        welcome_btnm_set_selected(btnm, sel);
        welcome_activate_menu(welcome_menu_selected);
        welcome_schedule_epd_refresh(250);
        return;
    }
}

// 屏幕销毁回调 - 清理资源和状态
static void welcome_screen_destroy_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Welcome screen destroyed, resetting state");

    // 重置全局状态
    welcome_menu_selected = 0;
    welcome_menu_btnm = NULL;
    welcome_indev = NULL;

    // 清空按键队列
    key_queue_init();

    // 停止刷新定时器
    if (welcome_refresh_timer != NULL) {
        lv_timer_delete(welcome_refresh_timer);
        welcome_refresh_timer = NULL;
    }
    welcome_last_epd_refresh_ms = 0;
}

// 创建主屏幕（Monster For Pan 菜单）
void lvgl_demo_create_welcome_screen(uint32_t battery_mv, uint8_t battery_pct, bool charging, const char *version_str, lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Creating Monster For Pan menu screen");

    // 保存输入设备指针
    welcome_indev = indev;

    // 初始化按键队列
    key_queue_init();

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 注册屏幕销毁回调 - 清理资源和状态
    lv_obj_add_event_cb(screen, welcome_screen_destroy_cb, LV_EVENT_DELETE, NULL);

    // 设置背景为白色（EPD 默认背景）
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
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
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // 副标题 "ESP32-C3 System"
    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "ESP32-C3 System");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_black(), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 40);

    // 顶部分隔线
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
    snprintf(bat_str, sizeof(bat_str), "Battery: %lu mV (%u%%)", battery_mv, battery_pct);
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
    // 中部分隔线
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
    // 菜单按钮矩阵 (Button Matrix)
    // ========================================
    // 定义菜单按钮映射表：
    // - 每个字符串是一个按钮的文本
    // - "\n" 表示换行，开始新的一行按钮
    // - 最后的 "" 是数组结束标记（必需）
    // - 共3个菜单项，每行一个按钮
    static const char *welcome_btnm_map[] = {
        "1. SDCard File Browser", "\n",  // 第1个按钮：文件浏览器，然后换行
        "2. BLE Reader", "\n",    // 第2个按钮：BLE阅读器，然后换行
        "3. Settings", ""         // 第3个按钮：设置，数组结束
    };

    // 创建按钮矩阵对象
    welcome_menu_btnm = lv_btnmatrix_create(screen);

    // 设置按钮映射表（定义了按钮的数量和文本）
    lv_btnmatrix_set_map(welcome_menu_btnm, welcome_btnm_map);

    // 设置按钮矩阵的尺寸（宽度 x 高度）
    // 宽度 440 像素：适合屏幕宽度（480px）减去左右边距
    // 高度 200 像素：容纳3个按钮行，每行约 60-70px
    lv_obj_set_size(welcome_menu_btnm, 440, 200);

    // 对齐按钮矩阵位置
    // - LV_ALIGN_TOP_LEFT: 相对于父容器左上角对齐
    // - x偏移 20: 左边距（与上方标签对齐）
    // - y偏移 200: 距离屏幕顶部200像素的位置（在菜单标题下方）
    lv_obj_align(welcome_menu_btnm, LV_ALIGN_TOP_LEFT, 20, 200);

    // ========================================
    // 按钮矩阵样式设置
    // ========================================
    // 启用内容裁剪：确保按钮文本不会溢出到容器外部
    // 这是修复左右两侧出现文本片段的关键
    lv_obj_set_style_clip_corner(welcome_menu_btnm, true, 0);

    // 设置整体内边距（4像素）：控制按钮与容器边界的间距
    lv_obj_set_style_pad_all(welcome_menu_btnm, 4, 0);

    // 设置行间距（10像素）：控制按钮行之间的垂直间距
    lv_obj_set_style_pad_row(welcome_menu_btnm, 10, 0);

    // 设置列间距（0像素）：因为每行只有一个按钮，不需要列间距
    lv_obj_set_style_pad_column(welcome_menu_btnm, 0, 0);

    // 移除边框：无边框的简洁样式
    lv_obj_set_style_border_width(welcome_menu_btnm, 0, 0);

    // 移除圆角：按钮采用直角样式（radius=0）
    lv_obj_set_style_radius(welcome_menu_btnm, 0, 0);

    // ----------------------------------------
    // 按钮默认状态样式（未选中时）
    // ----------------------------------------
    // 背景色：白色
    // - LV_PART_ITEMS: 指定样式作用于按钮项（而非整个容器）
    // - LV_STATE_DEFAULT: 指定样式在默认状态下生效
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_DEFAULT);

    // 背景透明度：完全不透明（LV_OPA_COVER = 255）
    lv_obj_set_style_bg_opa(welcome_menu_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);

    // 文字颜色：黑色
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_DEFAULT);

    // 字体：使用 14号 Montserrat 字体
    lv_obj_set_style_text_font(welcome_menu_btnm, &lv_font_montserrat_14, LV_PART_ITEMS);

    // 文字对齐：左对齐（按钮文本从左侧开始显示）
    lv_obj_set_style_text_align(welcome_menu_btnm, LV_TEXT_ALIGN_LEFT, LV_PART_ITEMS);

    // ----------------------------------------
    // 按钮选中状态样式（CHECKED状态）
    // ----------------------------------------
    // 背景色：黑色（与默认状态相反）
    // - LV_STATE_CHECKED: 指定样式在选中状态下生效
    lv_obj_set_style_bg_color(welcome_menu_btnm, lv_color_black(), LV_PART_ITEMS | LV_STATE_CHECKED);

    // 文字颜色：白色（与默认状态相反，形成反色效果）
    lv_obj_set_style_text_color(welcome_menu_btnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);

    // 圆角：选中时也保持直角样式
    lv_obj_set_style_radius(welcome_menu_btnm, 0, LV_PART_ITEMS | LV_STATE_CHECKED);

    // ========================================
    // 按钮控制属性设置
    // ========================================
    // 将所有按钮设置为可选中状态（CHECKABLE）
    // - 允许按钮通过点击或按键被选中/取消选中
    lv_btnmatrix_set_btn_ctrl_all(welcome_menu_btnm, LV_BTNMATRIX_CTRL_CHECKABLE);

    // 启用单选模式：同一时间只能有一个按钮被选中
    // - 当选中一个按钮时，自动取消其他按钮的选中状态
    lv_btnmatrix_set_one_checked(welcome_menu_btnm, true);

    // ----------------------------------------
    // 初始化默认选中项
    // ----------------------------------------
    // 设置全局变量：默认选中第1个菜单项（索引0）
    welcome_menu_selected = 0;

    // 设置按钮矩阵的当前选中按钮为索引0
    lv_btnmatrix_set_selected_btn(welcome_menu_btnm, welcome_menu_selected);

    // 将索引0的按钮设置为选中状态（应用 CHECKED 样式）
    lv_btnmatrix_set_btn_ctrl(welcome_menu_btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);

    // ========================================
    // 第4部分: 底部操作提示
    // ========================================
    lv_obj_t *line_bottom = lv_line_create(screen);
    static lv_point_precise_t line_bottom_points[] = {{10, 720}, {470, 720}};
    lv_line_set_points(line_bottom, line_bottom_points, 2);
    lv_obj_set_style_line_width(line_bottom, 2, 0);
    lv_obj_set_style_line_color(line_bottom, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_bottom, LV_OPA_COVER, 0);

    // 操作提示
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

    // 版本信息 (右下角)
    if (version_str && strlen(version_str) > 0) {
        lv_obj_t *version_label = lv_label_create(screen);
        lv_label_set_text(version_label, version_str);
        lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(version_label, lv_color_black(), 0);
        lv_obj_align(version_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    // ========================================
    // 按键处理：不使用 group，直接在对象上处理 KEY 事件
    // ========================================
    // 添加事件回调到 btnmatrix
    lv_obj_add_event_cb(welcome_menu_btnm, welcome_menu_btnm_event_cb, LV_EVENT_ALL, NULL);

    // 设置焦点到菜单，以便接收按键事件
    // 在 LVGL 9.x 中，使用 lv_group_t 来管理输入焦点
    if (indev != NULL) {
        // 创建一个 group 并将菜单对象添加进去
        lv_group_t *group = lv_group_create();
        lv_group_add_obj(group, welcome_menu_btnm);
        lv_indev_set_group(indev, group);
        ESP_LOGI(TAG, "Focus set to menu btnmatrix via group");
    }

    ESP_LOGI(TAG, "Monster For Pan menu screen created successfully");
}

// ============================================================================
// SD 卡文件浏览器功能
// ============================================================================

// 文件浏览器状态
typedef struct {
    char current_path[MAX_PATH_LEN];
    char file_names[MAX_FILES][64];
    bool is_directory[MAX_FILES];
    int file_count;
    int selected_index;
    lv_obj_t *file_list;
    lv_obj_t *path_label;
} file_browser_state_t;

static file_browser_state_t fb_state = {0};

// 前置声明
static bool read_directory(const char *path);
static void update_file_list_display(void);
static void file_browser_event_cb(lv_event_t *e);
static void file_browser_key_event_cb(lv_event_t *e);

// 读取目录内容
static bool read_directory(const char *path)
{
    ESP_LOGI(TAG, "Reading directory: %s", path);

    // 重置状态
    fb_state.file_count = 0;
    fb_state.selected_index = 0;

    // 保存当前路径
    strncpy(fb_state.current_path, path, MAX_PATH_LEN - 1);
    fb_state.current_path[MAX_PATH_LEN - 1] = '\0';

    // 打开目录
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return false;
    }

    struct dirent *entry;
    int dir_count = 0;
    int file_count = 0;

    // 先读取所有条目
    while ((entry = readdir(dir)) != NULL && fb_state.file_count < MAX_FILES) {
        const char *name = entry->d_name;

        // 跳过 "." 和 ".."
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // 构建完整路径
        char full_path[MAX_PATH_LEN];
        int len = snprintf(full_path, MAX_PATH_LEN - 1, "%s/%s", path, name);
        if (len >= MAX_PATH_LEN - 1) {
            ESP_LOGW(TAG, "Path truncated: %s/%s", path, name);
            full_path[MAX_PATH_LEN - 1] = '\0';
        }

        // 检查是否是目录
        struct stat st;
        if (stat(full_path, &st) == 0) {
            bool is_dir = S_ISDIR(st.st_mode);

            // 目录排在前面，文件排在后面
            if (is_dir) {
                // 将目录插入到目录组的末尾
                for (int i = fb_state.file_count; i > dir_count; i--) {
                    strncpy(fb_state.file_names[i], fb_state.file_names[i - 1], 63);
                    fb_state.file_names[i][63] = '\0';
                    fb_state.is_directory[i] = fb_state.is_directory[i - 1];
                }
                strncpy(fb_state.file_names[dir_count], name, 63);
                fb_state.file_names[dir_count][63] = '\0';
                fb_state.is_directory[dir_count] = true;
                dir_count++;
                fb_state.file_count++;
            } else {
                // 文件添加到末尾
                strncpy(fb_state.file_names[fb_state.file_count], name, 63);
                fb_state.file_names[fb_state.file_count][63] = '\0';
                fb_state.is_directory[fb_state.file_count] = false;
                file_count++;
                fb_state.file_count++;
            }
        }
    }

    closedir(dir);

    ESP_LOGI(TAG, "Found %d files (%d directories, %d files) in %s",
             fb_state.file_count, dir_count, file_count, path);

    return true;
}

// 更新文件列表显示
static void update_file_list_display(void)
{
    if (fb_state.file_list == NULL) {
        return;
    }

    // 删除并重新创建列表（替代 lv_list_clean，在 LVGL 9.x 中可能不可用）
    // 获取父对象和位置信息
    lv_obj_t *parent = lv_obj_get_parent(fb_state.file_list);
    int32_t x = lv_obj_get_x(fb_state.file_list);
    int32_t y = lv_obj_get_y(fb_state.file_list);
    int32_t w = lv_obj_get_width(fb_state.file_list);
    int32_t h = lv_obj_get_height(fb_state.file_list);

    // 删除旧列表
    lv_obj_delete(fb_state.file_list);

    // 创建新列表
    fb_state.file_list = lv_list_create(parent);
    lv_obj_set_size(fb_state.file_list, w, h);
    lv_obj_set_pos(fb_state.file_list, x, y);

    // 设置列表样式
    lv_obj_set_style_bg_color(fb_state.file_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fb_state.file_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fb_state.file_list, 1, 0);
    lv_obj_set_style_border_color(fb_state.file_list, lv_color_black(), 0);

    // 重新添加事件回调
    lv_obj_add_event_cb(fb_state.file_list, file_browser_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(fb_state.file_list, file_browser_key_event_cb, LV_EVENT_KEY, NULL);

    // 添加 ".." 返回上一项（如果不是根目录）
    if (strcmp(fb_state.current_path, SDCARD_MOUNT_POINT) != 0) {
        lv_obj_t *btn = lv_list_add_button(fb_state.file_list, LV_SYMBOL_LEFT, "..");
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
    }

    // 添加文件和目录
    for (int i = 0; i < fb_state.file_count; i++) {
        const char *icon = fb_state.is_directory[i] ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        lv_obj_t *btn = lv_list_add_button(fb_state.file_list, icon, fb_state.file_names[i]);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);

        // 设置选中状态
        if (i == fb_state.selected_index) {
            lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(btn, lv_color_white(), 0);
        }
    }

    // 更新路径标签
    if (fb_state.path_label != NULL) {
        // 显示相对路径（去掉 /sdcard 前缀）
        const char *display_path = fb_state.current_path;
        if (strncmp(fb_state.current_path, SDCARD_MOUNT_POINT, strlen(SDCARD_MOUNT_POINT)) == 0) {
            display_path = fb_state.current_path + strlen(SDCARD_MOUNT_POINT);
            if (*display_path == '/') {
                display_path++;
            }
            if (*display_path == '\0') {
                display_path = "/";
            }
        }

        char path_text[MAX_PATH_LEN + 20];
        int path_len = snprintf(path_text, sizeof(path_text) - 1, "Path: %.250s", display_path);
        if (path_len >= (int)(sizeof(path_text) - 1)) {
            path_text[sizeof(path_text) - 1] = '\0';
        }
        lv_label_set_text(fb_state.path_label, path_text);
    }
}

// 文件浏览器事件回调
static void file_browser_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {
        // 获取点击的按钮文本
        const char *btn_text = lv_list_get_button_text(fb_state.file_list, target);
        if (btn_text == NULL) {
            return;
        }

        ESP_LOGI(TAG, "File browser clicked: %s", btn_text);

        // 处理 ".." 返回上一级
        if (strcmp(btn_text, "..") == 0) {
            // 找到最后一个 '/'
            char *last_slash = strrchr(fb_state.current_path, '/');
            if (last_slash != NULL && last_slash != fb_state.current_path) {
                *last_slash = '\0';
                if (strlen(fb_state.current_path) == 0) {
                    strcpy(fb_state.current_path, SDCARD_MOUNT_POINT);
                }
                read_directory(fb_state.current_path);
                update_file_list_display();
            }
            return;
        }

        // 查找点击的文件索引
        for (int i = 0; i < fb_state.file_count; i++) {
            if (strcmp(btn_text, fb_state.file_names[i]) == 0) {
                if (fb_state.is_directory[i]) {
                    // 进入目录
                    char new_path[MAX_PATH_LEN];
                    int new_path_len = snprintf(new_path, MAX_PATH_LEN - 1, "%s/%s",
                                               fb_state.current_path, fb_state.file_names[i]);
                    if (new_path_len >= MAX_PATH_LEN - 1) {
                        new_path[MAX_PATH_LEN - 1] = '\0';
                    }
                    read_directory(new_path);
                    update_file_list_display();
                } else {
                    // 选中文件
                    ESP_LOGI(TAG, "Selected file: %s/%s", fb_state.current_path, fb_state.file_names[i]);
                }
                break;
            }
        }
    }
}

// 文件浏览器按键处理
static void file_browser_key_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);

        if (key == LV_KEY_UP) {
            // 向上选择
            if (fb_state.selected_index > 0) {
                fb_state.selected_index--;
                update_file_list_display();
            }
        } else if (key == LV_KEY_DOWN) {
            // 向下选择
            if (fb_state.selected_index < fb_state.file_count - 1) {
                fb_state.selected_index++;
                update_file_list_display();
            }
        } else if (key == LV_KEY_ENTER) {
            // 确认键：进入目录或选择文件
            if (fb_state.selected_index >= 0 && fb_state.selected_index < fb_state.file_count) {
                if (fb_state.is_directory[fb_state.selected_index]) {
                    // 进入目录
                    char new_path[MAX_PATH_LEN];
                    int new_path_len = snprintf(new_path, MAX_PATH_LEN - 1, "%s/%s",
                                               fb_state.current_path,
                                               fb_state.file_names[fb_state.selected_index]);
                    if (new_path_len >= MAX_PATH_LEN - 1) {
                        new_path[MAX_PATH_LEN - 1] = '\0';
                    }
                    read_directory(new_path);
                    update_file_list_display();
                } else {
                    // 选中文件
                    ESP_LOGI(TAG, "Selected file: %s/%s",
                             fb_state.current_path,
                             fb_state.file_names[fb_state.selected_index]);
                }
            }
        } else if (key == LV_KEY_ESC) {
            // 返回键：返回上一级目录或退出
            if (strcmp(fb_state.current_path, SDCARD_MOUNT_POINT) != 0) {
                char *last_slash = strrchr(fb_state.current_path, '/');
                if (last_slash != NULL && last_slash != fb_state.current_path) {
                    *last_slash = '\0';
                    if (strlen(fb_state.current_path) == 0) {
                        strcpy(fb_state.current_path, SDCARD_MOUNT_POINT);
                    }
                    read_directory(fb_state.current_path);
                    update_file_list_display();
                }
            } else {
                // 已经在根目录，返回主菜单
                ESP_LOGI(TAG, "Exiting file browser");
                // 这里可以添加返回主菜单的代码
            }
        }
    }
}

// 屏幕销毁回调
static void file_browser_screen_destroy_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "File browser screen destroyed");

    // 重置状态
    memset(&fb_state, 0, sizeof(file_browser_state_t));
}

// 创建 SD 卡文件浏览器页面
void lvgl_demo_create_file_browser_screen(lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Creating SD card file browser screen");

    // 初始化状态
    memset(&fb_state, 0, sizeof(file_browser_state_t));
    strcpy(fb_state.current_path, SDCARD_MOUNT_POINT);

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 注册屏幕销毁回调
    lv_obj_add_event_cb(screen, file_browser_screen_destroy_cb, LV_EVENT_DELETE, NULL);

    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // ========================================
    // 顶部标题区域
    // ========================================
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "SD Card File Browser");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // 顶部分隔线
    lv_obj_t *line_top = lv_line_create(screen);
    static lv_point_precise_t line_top_points[] = {{10, 40}, {470, 40}};
    lv_line_set_points(line_top, line_top_points, 2);
    lv_obj_set_style_line_width(line_top, 2, 0);
    lv_obj_set_style_line_color(line_top, lv_color_black(), 0);
    lv_obj_set_style_line_opa(line_top, LV_OPA_COVER, 0);

    // ========================================
    // 当前路径显示
    // ========================================
    fb_state.path_label = lv_label_create(screen);
    lv_obj_set_style_text_font(fb_state.path_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fb_state.path_label, lv_color_black(), 0);
    lv_obj_align(fb_state.path_label, LV_ALIGN_TOP_LEFT, 20, 50);

    // ========================================
    // 文件列表
    // ========================================
    fb_state.file_list = lv_list_create(screen);
    lv_obj_set_size(fb_state.file_list, 440, 620);
    lv_obj_align(fb_state.file_list, LV_ALIGN_TOP_LEFT, 20, 80);

    // 设置列表样式
    lv_obj_set_style_bg_color(fb_state.file_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fb_state.file_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fb_state.file_list, 1, 0);
    lv_obj_set_style_border_color(fb_state.file_list, lv_color_black(), 0);

    // 添加事件回调
    lv_obj_add_event_cb(fb_state.file_list, file_browser_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(fb_state.file_list, file_browser_key_event_cb, LV_EVENT_KEY, NULL);

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
    lv_label_set_text(hint1, "Vol+/-: Select file");
    lv_obj_set_style_text_font(hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint1, lv_color_black(), 0);
    lv_obj_align(hint1, LV_ALIGN_TOP_LEFT, 20, 730);

    lv_obj_t *hint2 = lv_label_create(screen);
    lv_label_set_text(hint2, "Confirm(3): Open dir");
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint2, lv_color_black(), 0);
    lv_obj_align(hint2, LV_ALIGN_TOP_LEFT, 20, 750);

    lv_obj_t *hint3 = lv_label_create(screen);
    lv_label_set_text(hint3, "Back(4): Return");
    lv_obj_set_style_text_font(hint3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint3, lv_color_black(), 0);
    lv_obj_align(hint3, LV_ALIGN_TOP_LEFT, 20, 770);

    // 设置焦点
    if (indev != NULL) {
        lv_group_t *group = lv_group_create();
        lv_group_add_obj(group, fb_state.file_list);
        lv_indev_set_group(indev, group);
    }

    // 读取根目录并显示
    if (read_directory(SDCARD_MOUNT_POINT)) {
        update_file_list_display();
    } else {
        ESP_LOGE(TAG, "Failed to read SD card root directory");

        // 显示错误消息
        lv_obj_t *error_label = lv_label_create(screen);
        lv_label_set_text(error_label, "No SD card found or read error!");
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(error_label, lv_color_black(), 0);
        lv_obj_align(error_label, LV_ALIGN_CENTER, 0, 0);
    }

    ESP_LOGI(TAG, "SD card file browser screen created successfully");
}
