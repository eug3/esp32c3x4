/**
 * @file file_browser.c
 * @brief SD 卡文件浏览器屏幕
 */

#include "file_browser.h"
#include "../lvgl_driver.h"
#include "screen_manager.h"
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
} file_browser_state_t;

static file_browser_state_t fb_state = {0};

// 前置声明
static bool read_directory(const char *path);
static void update_file_list_display(void);
static void file_browser_key_event_cb(lv_event_t *e);
static void file_browser_screen_destroy_cb(lv_event_t *e);

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

    // 删除并重新创建列表
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

    // 只添加键盘事件回调（无触摸屏）
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

    // 手动刷新模式：触发渲染
    for (int i = 0; i < 3; i++) {
        lvgl_trigger_render(NULL);
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
                // 手动刷新模式：先触发 LVGL 渲染
                lvgl_trigger_render(NULL);
                // 触发 EPD 局部刷新
                lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
                lvgl_display_refresh();
            }
        } else if (key == LV_KEY_DOWN) {
            // 向下选择
            if (fb_state.selected_index < fb_state.file_count - 1) {
                fb_state.selected_index++;
                update_file_list_display();
                // 手动刷新模式：先触发 LVGL 渲染
                lvgl_trigger_render(NULL);
                // 触发 EPD 局部刷新
                lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
                lvgl_display_refresh();
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
                    // 手动刷新模式：先触发 LVGL 渲染
                    lvgl_trigger_render(NULL);
                    // 进入目录时使用快速刷新（全屏变化）
                    lvgl_set_refresh_mode(EPD_REFRESH_FAST);
                    lvgl_display_refresh();
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
                    // 手动刷新模式：先触发 LVGL 渲染
                    lvgl_trigger_render(NULL);
                    // 返回上级目录时使用快速刷新
                    lvgl_set_refresh_mode(EPD_REFRESH_FAST);
                    lvgl_display_refresh();
                }
            } else {
                // 已经在根目录，返回主菜单
                ESP_LOGI(TAG, "Exiting file browser, returning to welcome screen");

                // 重置刷新状态
                lvgl_reset_refresh_state();

                // 返回主菜单（使用屏幕管理器）
                screen_manager_show_index();
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
void file_browser_screen_create(lv_indev_t *indev)
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

    // 只添加键盘事件回调（无触摸屏）
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
        lv_group_set_wrap(group, true);  // 启用循环导航
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

    // 强制 LVGL 处理渲染（手动刷新模式）
    // 先使整个屏幕无效，强制 LVGL 重新渲染所有内容
    lv_obj_invalidate(screen);

    for (int i = 0; i < 5; i++) {
        lvgl_trigger_render(NULL);  // 手动触发渲染
        vTaskDelay(pdMS_TO_TICKS(5));  // 等待渲染完成
    }

    // 等待渲染完全完成
    vTaskDelay(pdMS_TO_TICKS(50));

    // 重置刷新状态（清除旧脏区域和计数器）
    lvgl_reset_refresh_state();

    // 触发 EPD 刷新
    lvgl_set_refresh_mode(EPD_REFRESH_FAST);
    lvgl_display_refresh();

    // 等待刷新完成（最多 2 秒）
    ESP_LOGI(TAG, "Waiting for EPD refresh to complete...");
    int wait_count = 0;
    while (lvgl_is_refreshing() && wait_count < 200) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }

    if (wait_count >= 200) {
        ESP_LOGW(TAG, "EPD refresh timeout after %d attempts", wait_count);
    } else {
        ESP_LOGI(TAG, "EPD refresh completed in %d ms", wait_count * 10);
    }

    ESP_LOGI(TAG, "SD card file browser screen created successfully");
}
