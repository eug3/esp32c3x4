/**
 * @file file_browser.c
 * @brief SD 卡文件浏览器屏幕
 */

#include "file_browser.h"
#include "../lvgl_driver.h"
#include "screen_manager.h"
#include "font_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "FILE_BROWSER";

#define SDCARD_MOUNT_POINT "/sdcard"
#define MAX_FILES 100
#define MAX_PATH_LEN 256

// 文件浏览器状态
typedef struct {
    char current_path[MAX_PATH_LEN];
    char file_names[MAX_FILES][64];
    bool is_directory[MAX_FILES];
    int file_count;
    int selected_index;
    lv_obj_t *file_list;
    lv_obj_t *path_label;

    lv_indev_t *indev;
    lv_group_t *group;
    lv_obj_t *row_btns[MAX_FILES + 1];
    int row_btn_to_index[MAX_FILES + 1];
    int row_btn_count;

    // 避免在事件回调中直接 lv_obj_clean()/重建 UI（会删除正在处理事件的对象，导致崩溃）
    enum {
        FB_ACTION_NONE = 0,
        FB_ACTION_OPEN_DIR,
        FB_ACTION_GO_UP,
        FB_ACTION_EXIT,
    } pending_action;
    int pending_index;
} file_browser_state_t;

static file_browser_state_t fb_state = {0};

// 前置声明
static bool read_directory(const char *path);
static void update_file_list_display(void);
static void file_browser_row_key_event_cb(lv_event_t *e);
static void file_browser_row_focused_cb(lv_event_t *e);
static void file_browser_screen_destroy_cb(lv_event_t *e);
static void file_browser_process_pending_action_cb(void *user_data);

static void set_row_selected(lv_obj_t *btn, bool selected)
{
    if (btn == NULL) {
        return;
    }

    lv_obj_t *icon_lbl = lv_obj_get_child(btn, 0);
    lv_obj_t *text_lbl = lv_obj_get_child(btn, 1);

    if (selected) {
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        if (icon_lbl) lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        if (text_lbl) lv_obj_set_style_text_color(text_lbl, lv_color_white(), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        if (icon_lbl) lv_obj_set_style_text_color(icon_lbl, lv_color_black(), 0);
        if (text_lbl) lv_obj_set_style_text_color(text_lbl, lv_color_black(), 0);
    }
}

static void file_browser_schedule_action(int action, int index)
{
    fb_state.pending_action = action;
    fb_state.pending_index = index;
    (void)lv_async_call(file_browser_process_pending_action_cb, NULL);
}

static void file_browser_process_pending_action_cb(void *user_data)
{
    (void)user_data;

    const int action = fb_state.pending_action;
    const int idx = fb_state.pending_index;

    fb_state.pending_action = FB_ACTION_NONE;
    fb_state.pending_index = 0;

    if (action == FB_ACTION_NONE) {
        return;
    }

    if (action == FB_ACTION_EXIT) {
        ESP_LOGI(TAG, "Exiting file browser, returning to welcome screen");
        lvgl_reset_refresh_state();
        screen_manager_show_index();
        return;
    }

    if (action == FB_ACTION_GO_UP) {
        if (strcmp(fb_state.current_path, SDCARD_MOUNT_POINT) != 0) {
            char *last_slash = strrchr(fb_state.current_path, '/');
            if (last_slash != NULL && last_slash != fb_state.current_path) {
                *last_slash = '\0';
                if (strlen(fb_state.current_path) == 0) {
                    strcpy(fb_state.current_path, SDCARD_MOUNT_POINT);
                }
                if (read_directory(fb_state.current_path)) {
                    update_file_list_display();
                    lvgl_trigger_render(NULL);
                    // 组件内操作：目录导航使用局刷
                    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
                    lvgl_display_refresh();
                }
            }
        }
        return;
    }

    if (action == FB_ACTION_OPEN_DIR) {
        if (idx < 0 || idx >= fb_state.file_count) {
            return;
        }

        if (!fb_state.is_directory[idx]) {
            ESP_LOGI(TAG, "Selected file: %s/%s", fb_state.current_path, fb_state.file_names[idx]);
            return;
        }

        char new_path[MAX_PATH_LEN];
        int new_path_len = snprintf(new_path, MAX_PATH_LEN - 1, "%s/%s",
                                    fb_state.current_path,
                                    fb_state.file_names[idx]);
        if (new_path_len >= MAX_PATH_LEN - 1) {
            new_path[MAX_PATH_LEN - 1] = '\0';
        }

        if (read_directory(new_path)) {
            update_file_list_display();
            lvgl_trigger_render(NULL);
            // 组件内操作：目录导航使用局刷
            lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
            lvgl_display_refresh();
        }
        return;
    }
}

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

    // 重建 group，确保 group 内对象指针不悬挂（list 子项会被清空重建）
    if (fb_state.indev != NULL) {
        if (fb_state.group != NULL) {
            lv_group_del(fb_state.group);
            fb_state.group = NULL;
        }
        fb_state.group = lv_group_create();
        lv_group_set_wrap(fb_state.group, true);
        lv_indev_set_group(fb_state.indev, fb_state.group);
    }

    // 不要删除/重建 list：
    // - 会让 lv_group 里绑定的对象失效，导致按键事件不再投递
    // - 也容易引入样式/布局的不可预期问题
    // 这里仅清空子对象并重新填充
    lv_obj_clean(fb_state.file_list);

    // 设置列表样式
    lv_obj_set_style_bg_color(fb_state.file_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fb_state.file_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fb_state.file_list, 1, 0);
    lv_obj_set_style_border_color(fb_state.file_list, lv_color_black(), 0);
    // 关键：显式设置列表项文字颜色，避免主题/默认样式导致“白底白字看不见”
    lv_obj_set_style_text_color(fb_state.file_list, lv_color_black(), LV_PART_ITEMS);

    fb_state.row_btn_count = 0;

    // 添加 ".." 返回上一项（如果不是根目录）
    if (strcmp(fb_state.current_path, SDCARD_MOUNT_POINT) != 0) {
        lv_obj_t *btn = lv_list_add_button(fb_state.file_list, LV_SYMBOL_LEFT, "..");
        // list button 内部包含两个 label（图标/文字），这里显式设置字体和颜色
        lv_obj_t *icon = lv_obj_get_child(btn, 0);
        lv_obj_t *txt = lv_obj_get_child(btn, 1);
        if (icon) {
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(icon, lv_color_black(), 0);
        }
        if (txt) {
            lv_obj_set_style_text_font(txt, font_manager_get_font(), 0);
            lv_obj_set_style_text_color(txt, lv_color_black(), 0);
        }

        lv_obj_add_event_cb(btn, file_browser_row_key_event_cb, LV_EVENT_KEY, &fb_state.row_btn_to_index[fb_state.row_btn_count]);
        lv_obj_add_event_cb(btn, file_browser_row_focused_cb, LV_EVENT_FOCUSED, &fb_state.row_btn_to_index[fb_state.row_btn_count]);
        fb_state.row_btns[fb_state.row_btn_count] = btn;
        fb_state.row_btn_to_index[fb_state.row_btn_count] = -1;
        fb_state.row_btn_count++;
        if (fb_state.group) {
            lv_group_add_obj(fb_state.group, btn);
        }
    }

    // 添加文件和目录
    for (int i = 0; i < fb_state.file_count; i++) {
        const char *icon = fb_state.is_directory[i] ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        lv_obj_t *btn = lv_list_add_button(fb_state.file_list, icon, fb_state.file_names[i]);
        // 默认项：白底黑字（显式设置到子 label，避免继承不生效）
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        lv_obj_t *icon_lbl = lv_obj_get_child(btn, 0);
        lv_obj_t *text_lbl = lv_obj_get_child(btn, 1);
        if (icon_lbl) {
            lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(icon_lbl, lv_color_black(), 0);
        }
        if (text_lbl) {
            lv_obj_set_style_text_font(text_lbl, font_manager_get_font(), 0);
            lv_obj_set_style_text_color(text_lbl, lv_color_black(), 0);
        }

        lv_obj_add_event_cb(btn, file_browser_row_key_event_cb, LV_EVENT_KEY, &fb_state.row_btn_to_index[fb_state.row_btn_count]);
        lv_obj_add_event_cb(btn, file_browser_row_focused_cb, LV_EVENT_FOCUSED, &fb_state.row_btn_to_index[fb_state.row_btn_count]);
        fb_state.row_btns[fb_state.row_btn_count] = btn;
        fb_state.row_btn_to_index[fb_state.row_btn_count] = i;
        fb_state.row_btn_count++;
        if (fb_state.group) {
            lv_group_add_obj(fb_state.group, btn);
        }

        // 设置选中状态（用于 EPD 上更显著的高亮）
        if (i == fb_state.selected_index) {
            set_row_selected(btn, true);
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

    // 把焦点显式放在当前选中的行（避免 NEXT/PREV 被 group 吞掉但焦点未建立）
    if (fb_state.group && fb_state.row_btn_count > 0) {
        lv_obj_t *focus_btn = NULL;
        for (int j = 0; j < fb_state.row_btn_count; j++) {
            if (fb_state.row_btn_to_index[j] == fb_state.selected_index) {
                focus_btn = fb_state.row_btns[j];
                break;
            }
        }
        if (focus_btn == NULL) {
            // 如果 selected_index 无效（比如目录变更后），默认聚焦第一项（跳过 ".."）
            focus_btn = fb_state.row_btns[0];
            if (fb_state.row_btn_to_index[0] == -1 && fb_state.row_btn_count > 1) {
                focus_btn = fb_state.row_btns[1];
            }
        }
        lv_group_focus_obj(focus_btn);
    }

    // 手动刷新模式：触发渲染
    for (int i = 0; i < 3; i++) {
        lvgl_trigger_render(NULL);
    }
}

static void file_browser_row_focused_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) {
        return;
    }

    int *p_idx = (int *)lv_event_get_user_data(e);
    if (p_idx == NULL) {
        return;
    }

    const int new_idx = *p_idx;
    if (new_idx == fb_state.selected_index) {
        return;
    }

    // 取消旧高亮
    for (int i = 0; i < fb_state.row_btn_count; i++) {
        if (fb_state.row_btn_to_index[i] == fb_state.selected_index) {
            set_row_selected(fb_state.row_btns[i], false);
            break;
        }
    }

    fb_state.selected_index = new_idx;

    // 设置新高亮
    for (int i = 0; i < fb_state.row_btn_count; i++) {
        if (fb_state.row_btn_to_index[i] == fb_state.selected_index) {
            set_row_selected(fb_state.row_btns[i], true);
            break;
        }
    }

    // 手动刷新模式：触发渲染 + 刷新
    lvgl_trigger_render(NULL);
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_display_refresh();
}

// 行按钮按键处理：ENTER / ESC（导航键交给 lv_group）
static void file_browser_row_key_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    int *p_idx = (int *)lv_event_get_user_data(e);
    const int idx = (p_idx != NULL) ? *p_idx : fb_state.selected_index;

    if (key == LV_KEY_ENTER) {
        if (idx == -1) {
            file_browser_schedule_action(FB_ACTION_GO_UP, 0);
        } else {
            file_browser_schedule_action(FB_ACTION_OPEN_DIR, idx);
        }
    } else if (key == LV_KEY_ESC) {
        if (strcmp(fb_state.current_path, SDCARD_MOUNT_POINT) != 0) {
            file_browser_schedule_action(FB_ACTION_GO_UP, 0);
        } else {
            file_browser_schedule_action(FB_ACTION_EXIT, 0);
        }
    }
}

// 屏幕销毁回调
static void file_browser_screen_destroy_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "File browser screen destroyed");

    if (fb_state.group != NULL) {
        lv_group_del(fb_state.group);
        fb_state.group = NULL;
    }

    // 重置状态
    memset(&fb_state, 0, sizeof(file_browser_state_t));
}

// 创建 SD 卡文件浏览器页面
void file_browser_screen_create(lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Creating SD card file browser screen");

    // 初始化状态
    memset(&fb_state, 0, sizeof(file_browser_state_t));
    strcpy(fb_state.current_path, SDCARD_MOUNT_POINT);
    fb_state.indev = indev;
    fb_state.pending_action = FB_ACTION_NONE;
    fb_state.pending_index = 0;

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

    // 允许通过按键聚焦到列表上（无触摸屏场景）
    lv_obj_add_flag(fb_state.file_list, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // 设置列表样式
    lv_obj_set_style_bg_color(fb_state.file_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fb_state.file_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fb_state.file_list, 1, 0);
    lv_obj_set_style_border_color(fb_state.file_list, lv_color_black(), 0);

    // 只添加键盘事件回调（无触摸屏）
    // 说明：不在 list 容器上处理按键；改为每一行按钮加入 group，由 group 响应 NEXT/PREV 导航

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
        // group 在 update_file_list_display() 内按当前列表重建
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

    // 强制 LVGL 处理渲染（手动刷新模式）
    // 先使整个屏幕无效，强制 LVGL 重新渲染所有内容
    lv_obj_invalidate(screen);

    for (int i = 0; i < 5; i++) {
        lvgl_trigger_render(NULL);  // 手动触发渲染
        vTaskDelay(pdMS_TO_TICKS(5));  // 等待渲染完成
    }

    // 等待渲染完全完成
    vTaskDelay(pdMS_TO_TICKS(50));

    // 注意：不要在这里调用 lvgl_reset_refresh_state()
    // screen_manager 已经在切换屏幕时调用过了
    // 这里的脏区域记录是正确的，反映了 file_browser 的内容

    // 触发 EPD 刷新 - 由 screen_manager 设置刷新模式（组件间切换用 FULL）
    lvgl_display_refresh();

    ESP_LOGI(TAG, "SD card file browser screen created successfully");
}
