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

// UI对象 (未使用，已移除以节省内存)

// Welcome screen helpers (EPD refresh scheduling)
static lv_timer_t *welcome_refresh_timer = NULL;
static uint32_t welcome_last_epd_refresh_ms = 0;

// Welcome menu (use btnmatrix to avoid per-child label focus styling)
static lv_obj_t *welcome_menu_btnm = NULL;
static uint16_t welcome_menu_selected = 0;

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
static bool is_refreshing = false;

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

    // 刷新完成，处理队列中的按键
    is_refreshing = false;

    int16_t net_direction = key_queue_get_net_direction();

    if (net_direction != 0 && welcome_menu_btnm != NULL) {
        // 根据净方向计算新的选中项
        int16_t new_index = ((int16_t)welcome_menu_selected + net_direction) % 3;

        // 处理负数的情况
        if (new_index < 0) {
            new_index += 3;
        }

        ESP_LOGI(TAG, "Refresh complete: processing queued keys net_direction=%d, %d -> %d",
                 net_direction, welcome_menu_selected, new_index);

        // 应用新的选中项
        welcome_btnm_set_selected(welcome_menu_btnm, (uint16_t)new_index);

        // 立即开始下一轮刷新
        is_refreshing = true;
        welcome_last_epd_refresh_ms = lv_tick_get();
        lvgl_set_refresh_mode(EPD_REFRESH_FAST);
        lvgl_display_refresh();
    } else {
        ESP_LOGD(TAG, "Refresh complete: no queued keys to process");
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

    // 检查是否正在刷新或者距离上次刷新时间太短
    if (is_refreshing || (welcome_last_epd_refresh_ms != 0 && (now - welcome_last_epd_refresh_ms) < min_interval_ms)) {
        // 正在刷新，将按键加入队列
        ESP_LOGD(TAG, "Refreshing or too soon, queuing key (queue count: %u)", key_queue.count);
        key_queue_push(key_type);

        // 等待刷新完成后再处理
        if (!is_refreshing) {
            const uint32_t remain = min_interval_ms - (now - welcome_last_epd_refresh_ms);
            welcome_schedule_epd_refresh(remain + 50);
        }
        return;
    }

    // 没有在刷新，立即处理
    ESP_LOGD(TAG, "Not refreshing, processing key immediately");

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

        ESP_LOGI(TAG, "Processing key immediately: net_direction=%d, %d -> %d",
                 net_direction, welcome_menu_selected, new_index);

        // 应用新的选中项
        welcome_btnm_set_selected(welcome_menu_btnm, (uint16_t)new_index);

        // 立即刷新
        is_refreshing = true;
        welcome_last_epd_refresh_ms = now;
        lvgl_set_refresh_mode(EPD_REFRESH_FAST);
        lvgl_display_refresh();

        // 调度回调来处理后续可能入队的按键
        welcome_schedule_epd_refresh(min_interval_ms);
    }
}

static void welcome_activate_menu(uint16_t menu_index)
{
    ESP_LOGI(TAG, "Welcome menu activated: %u", (unsigned)menu_index);
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

    // Clear old selection（使用全局变量 welcome_menu_btnm）
    lv_btnmatrix_clear_btn_ctrl(welcome_menu_btnm, old_index, LV_BTNMATRIX_CTRL_CHECKED);

    // Update global state
    welcome_menu_selected = new_index;

    // Set new selection（使用全局变量 welcome_menu_btnm）
    lv_btnmatrix_set_selected_btn(welcome_menu_btnm, welcome_menu_selected);
    lv_btnmatrix_set_btn_ctrl(welcome_menu_btnm, welcome_menu_selected, LV_BTNMATRIX_CTRL_CHECKED);

    // 注意：不需要手动调用 lv_obj_invalidate()
    // LVGL 会在控件状态改变时自动标记脏区域并触发重绘
    // 手动调用可能导致重复绘制或绘制位置错误

    ESP_LOGD(TAG, "Menu selection changed: %u -> %u", old_index, new_index);
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

        // 检查key有效性
        if (key == 0) {
            ESP_LOGW(TAG, "Received invalid key (0)");
            return;
        }

        // 向上选择（按键5：音量+）
        if (key == LV_KEY_UP) {
            welcome_process_key(KEY_UP);
            return;
        }

        // 向下选择（按键6：音量-）
        if (key == LV_KEY_DOWN) {
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

    // 清空按键队列
    key_queue_init();
    is_refreshing = false;

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

    // 初始化按键队列
    key_queue_init();
    is_refreshing = false;

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
