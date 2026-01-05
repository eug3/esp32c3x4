/**
 * @file file_browser_screen.c
 * @brief 文件浏览器屏幕实现 - 手绘 UI 版本
 */

#include "file_browser_screen.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "FILE_BROWSER";

// 文件浏览器屏幕实例
static screen_t g_file_browser_screen = {0};

// 文件信息结构
typedef struct {
    char name[128];
    bool is_directory;
    off_t size;
} file_info_t;

// 浏览器状态
static struct {
    file_info_t *files;           // 文件列表
    int file_count;               // 文件总数
    int current_page;             // 当前页码（从0开始）
    int selected_index;           // 当前选中文件的索引（相对于当前页）
    int files_per_page;           // 每页显示的文件数
    char current_path[256];       // 当前路径
} s_browser_state = {
    .files = NULL,
    .file_count = 0,
    .current_page = 0,
    .selected_index = 0,
    .files_per_page = 10,
    .current_path = "/sdcard",
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_show(screen_t *screen);
static void on_hide(screen_t *screen);
static void on_draw(screen_t *screen);
static void on_event(screen_t *screen, button_t btn, button_event_t event);
static bool scan_directory(const char *path);
static void free_file_list(void);
static void draw_file_list(void);
static void draw_page_indicator(void);
static void draw_single_file(int display_index, const file_info_t *file, bool is_selected);
static void open_selected_file(void);
static void navigate_to_parent_directory(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 扫描目录，构建文件列表
 */
static bool scan_directory(const char *path)
{
    ESP_LOGI(TAG, "Scanning directory: %s", path);

    // 释放旧的文件列表
    free_file_list();

    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return false;
    }

    // 临时存储文件名（放到堆上，避免在 input_poll 等小栈任务里扫描目录时栈溢出）
    const int max_entries = 256;
    char **file_names = (char **)heap_caps_calloc(max_entries, sizeof(char *), MALLOC_CAP_8BIT);
    if (file_names == NULL) {
        ESP_LOGE(TAG, "Failed to allocate temp file name list");
        closedir(dir);
        return false;
    }
    int temp_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && temp_count < max_entries) {
        // 跳过隐藏文件（以.开头）
        if (entry->d_name[0] == '.') {
            continue;
        }

        // 分配内存存储文件名
        file_names[temp_count] = strdup(entry->d_name);
        if (file_names[temp_count] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for file name");
            break;
        }
        temp_count++;
    }
    closedir(dir);

    if (temp_count == 0) {
        ESP_LOGI(TAG, "No files found in directory");
        heap_caps_free(file_names);
        return true;
    }

    // 分配文件列表内存
    s_browser_state.files = (file_info_t *)heap_caps_malloc(temp_count * sizeof(file_info_t), MALLOC_CAP_8BIT);
    if (s_browser_state.files == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file list");
        for (int i = 0; i < temp_count; i++) {
            free(file_names[i]);
        }
        heap_caps_free(file_names);
        return false;
    }

    // 填充文件信息并排序（目录在前，文件在后）
    int dir_count = 0;
    int file_count = 0;

    for (int i = 0; i < temp_count; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, file_names[i]);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            bool is_dir = S_ISDIR(st.st_mode);

            if (is_dir) {
                // 目录插入到前面（使用 memmove 安全移动，避免元素拷贝越界）
                if (dir_count > 0 && dir_count < temp_count) {
                    memmove(&s_browser_state.files[1], &s_browser_state.files[0], dir_count * sizeof(file_info_t));
                }
                strncpy(s_browser_state.files[0].name, file_names[i], sizeof(s_browser_state.files[0].name) - 1);
                s_browser_state.files[0].name[sizeof(s_browser_state.files[0].name) - 1] = '\0';
                s_browser_state.files[0].is_directory = true;
                s_browser_state.files[0].size = 0;
                dir_count++;
            } else {
                // 文件添加到后面
                int idx = dir_count + file_count;
                strncpy(s_browser_state.files[idx].name, file_names[i], sizeof(s_browser_state.files[idx].name) - 1);
                s_browser_state.files[idx].name[sizeof(s_browser_state.files[idx].name) - 1] = '\0';
                s_browser_state.files[idx].is_directory = false;
                s_browser_state.files[idx].size = st.st_size;
                file_count++;
            }
        }
        free(file_names[i]);
    }

    heap_caps_free(file_names);

    s_browser_state.file_count = temp_count;
    ESP_LOGI(TAG, "Found %d files (%d directories, %d files)", temp_count, dir_count, file_count);

    return true;
}

/**
 * @brief 释放文件列表内存
 */
static void free_file_list(void)
{
    if (s_browser_state.files != NULL) {
        heap_caps_free(s_browser_state.files);
        s_browser_state.files = NULL;
    }
    s_browser_state.file_count = 0;
}

/**
 * @brief 绘制单个文件项
 */
static void draw_single_file(int display_index, const file_info_t *file, bool is_selected)
{
    sFONT *ui_font = display_get_default_ascii_font();

    // 计算位置
    int start_y = 80;
    int item_height = 50;
    int y = start_y + display_index * item_height;

    int inner_height = item_height - 8;
    int text_y = y + (inner_height - ui_font->Height) / 2;
    if (text_y < y) {
        text_y = y;
    }

    // 限制文本显示宽度
    int max_text_width = SCREEN_WIDTH - 100;
    char display_name[64];
    strncpy(display_name, file->name, sizeof(display_name) - 4);
    display_name[sizeof(display_name) - 4] = '\0';

    // 如果文本太长，添加省略号
    int text_width = display_get_text_width_font(display_name, ui_font);
    if (text_width > max_text_width) {
        int keep_chars = (max_text_width * strlen(display_name)) / text_width;
        if (keep_chars > 3) {
            strcpy(display_name + keep_chars - 3, "...");
        }
    }

    if (is_selected) {
        // 选中状态：黑底白字
        display_draw_rect(20, y, SCREEN_WIDTH - 40, item_height - 8, COLOR_BLACK, true);
        display_draw_text_font(30, text_y, display_name, ui_font, COLOR_WHITE, COLOR_BLACK);
    } else {
        // 普通状态：白底黑字
        display_draw_rect(20, y, SCREEN_WIDTH - 40, item_height - 8, COLOR_BLACK, false);
        display_draw_text_font(30, text_y, display_name, ui_font, COLOR_BLACK, COLOR_WHITE);
    }
}

/**
 * @brief 绘制文件列表
 */
static void draw_file_list(void)
{
    // 计算当前页的文件范围
    int start_idx = s_browser_state.current_page * s_browser_state.files_per_page;
    int end_idx = start_idx + s_browser_state.files_per_page;
    if (end_idx > s_browser_state.file_count) {
        end_idx = s_browser_state.file_count;
    }

    // 绘制文件项
    for (int i = start_idx; i < end_idx; i++) {
        int display_index = i - start_idx;
        bool is_selected = (display_index == s_browser_state.selected_index);
        draw_single_file(display_index, &s_browser_state.files[i], is_selected);
    }
}

/**
 * @brief 绘制页码指示器
 */
static void draw_page_indicator(void)
{
    int total_pages = (s_browser_state.file_count + s_browser_state.files_per_page - 1) / s_browser_state.files_per_page;
    if (total_pages <= 1) {
        return;
    }

    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d/%d", s_browser_state.current_page + 1, total_pages);

    sFONT *ui_font = display_get_default_ascii_font();
    int text_width = display_get_text_width_font(page_str, ui_font);
    display_draw_text_font(SCREEN_WIDTH - text_width - 20, SCREEN_HEIGHT - 30, page_str, ui_font, COLOR_BLACK, COLOR_WHITE);
}

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "File browser screen shown");

    // 扫描默认目录
    scan_directory(s_browser_state.current_path);

    // 重置浏览状态
    s_browser_state.current_page = 0;
    s_browser_state.selected_index = 0;

    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "File browser screen hidden");

    // 释放文件列表内存
    free_file_list();
}

static void on_draw(screen_t *screen)
{
    // 清屏
    display_clear(COLOR_WHITE);

    sFONT *ui_font = display_get_default_ascii_font();

    // 绘制标题栏
    display_draw_text_font(20, 20, "File Browser", ui_font, COLOR_BLACK, COLOR_WHITE);

    // 绘制当前路径
    int path_y = 50;
    char path_display[64];
    strncpy(path_display, s_browser_state.current_path, sizeof(path_display) - 1);
    path_display[sizeof(path_display) - 1] = '\0';

    // 如果路径太长，截断显示
    int path_width = display_get_text_width_font(path_display, ui_font);
    int max_path_width = SCREEN_WIDTH - 40;
    if (path_width > max_path_width) {
        int keep_chars = (max_path_width * strlen(path_display)) / path_width;
        if (keep_chars > 3) {
            strcpy(path_display, "...");
            strcat(path_display, path_display + strlen(path_display) - keep_chars + 3);
        }
    }
    display_draw_text_font(20, path_y, path_display, ui_font, COLOR_BLACK, COLOR_WHITE);

    // 绘制文件列表
    if (s_browser_state.file_count == 0) {
        display_draw_text_font(20, 150, "No files found", ui_font, COLOR_BLACK, COLOR_WHITE);
    } else {
        draw_file_list();
    }

    // 绘制页码指示器
    draw_page_indicator();

    // 绘制底部提示
    display_draw_text_font(20, SCREEN_HEIGHT - 60, "VOL+/-: Select  L/R: Page  CONFIRM: Open  BACK: Return",
                           ui_font, COLOR_BLACK, COLOR_WHITE);
}

/**
 * @brief 打开选中的文件/目录
 */
static void open_selected_file(void)
{
    int file_idx = s_browser_state.current_page * s_browser_state.files_per_page + s_browser_state.selected_index;
    if (file_idx >= s_browser_state.file_count) {
        return;
    }

    file_info_t *file = &s_browser_state.files[file_idx];

    if (file->is_directory) {
        // 进入子目录
        // 计算新路径长度，避免缓冲区溢出
        size_t path_len = strlen(s_browser_state.current_path);
        size_t name_len = strlen(file->name);

        if (path_len + name_len + 2 > sizeof(s_browser_state.current_path)) {
            ESP_LOGE(TAG, "Path too long, cannot enter directory");
            return;
        }

        // 直接在 current_path 后追加文件名
        s_browser_state.current_path[path_len] = '/';
        strncpy(s_browser_state.current_path + path_len + 1, file->name,
                sizeof(s_browser_state.current_path) - path_len - 1);
        s_browser_state.current_path[sizeof(s_browser_state.current_path) - 1] = '\0';

        // 扫描新目录
        scan_directory(s_browser_state.current_path);
        s_browser_state.current_page = 0;
        s_browser_state.selected_index = 0;

        ESP_LOGI(TAG, "Entered directory: %s", s_browser_state.current_path);

        // 立即重绘并刷新
        g_file_browser_screen.needs_redraw = true;
        screen_manager_draw();
        display_refresh(REFRESH_MODE_FULL);
    } else {
        // 打开文件
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", s_browser_state.current_path, file->name);
        ESP_LOGI(TAG, "Opening file: %s", full_path);

        // 根据文件扩展名决定如何打开
        const char *ext = strrchr(file->name, '.');
        if (ext != NULL) {
            if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".md") == 0) {
                // 文本文件 - 使用阅读器打开
                screen_manager_show_reader(full_path);
            } else if (strcasecmp(ext, ".epub") == 0) {
                // EPUB 电子书 - 使用阅读器打开
                screen_manager_show_reader(full_path);
            } else if (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".png") == 0 ||
                       strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
                // 图片文件 - 使用图片浏览器打开
                screen_manager_show_image_browser(full_path);
            } else {
                // 其他文件类型 - 尝试用阅读器打开（可能是二进制或文本）
                ESP_LOGI(TAG, "Opening file with reader (may not be supported): %s", full_path);
                screen_manager_show_reader(full_path);
            }
        } else {
            // 无扩展名文件 - 尝试用阅读器打开
            ESP_LOGI(TAG, "Opening file without extension: %s", full_path);
            screen_manager_show_reader(full_path);
        }
    }
}

/**
 * @brief 返回上级目录
 */
static void navigate_to_parent_directory(void)
{
    if (strcmp(s_browser_state.current_path, "/sdcard") == 0) {
        // 已经在根目录，返回到首页
        screen_manager_back();
        return;
    }

    // 查找最后一个斜杠
    char *last_slash = strrchr(s_browser_state.current_path, '/');
    if (last_slash != NULL && last_slash != s_browser_state.current_path) {
        *last_slash = '\0';
        scan_directory(s_browser_state.current_path);
        s_browser_state.current_page = 0;
        s_browser_state.selected_index = 0;
        ESP_LOGI(TAG, "Returned to parent directory: %s", s_browser_state.current_path);

        // 立即重绘并刷新
        g_file_browser_screen.needs_redraw = true;
        screen_manager_draw();
        display_refresh(REFRESH_MODE_FULL);
    }
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    int total_pages = (s_browser_state.file_count + s_browser_state.files_per_page - 1) / s_browser_state.files_per_page;
    int items_on_current_page = s_browser_state.file_count - s_browser_state.current_page * s_browser_state.files_per_page;
    if (items_on_current_page > s_browser_state.files_per_page) {
        items_on_current_page = s_browser_state.files_per_page;
    }

    switch (btn) {
        case BTN_LEFT:
            // 上一页
            if (s_browser_state.current_page > 0) {
                s_browser_state.current_page--;
                s_browser_state.selected_index = 0;
                screen_manager_request_redraw();
                ESP_LOGI(TAG, "Previous page: %d/%d", s_browser_state.current_page + 1, total_pages);
            }
            break;

        case BTN_RIGHT:
            // 下一页
            if (s_browser_state.current_page < total_pages - 1) {
                s_browser_state.current_page++;
                s_browser_state.selected_index = 0;
                screen_manager_request_redraw();
                ESP_LOGI(TAG, "Next page: %d/%d", s_browser_state.current_page + 1, total_pages);
            }
            break;

        case BTN_VOLUME_UP:
            // 向上选择（选中上一个文件）
            if (s_browser_state.selected_index > 0) {
                int old_selection = s_browser_state.selected_index;
                int new_selection = old_selection - 1;

                ESP_LOGI(TAG, "Focus changed: %d -> %d", old_selection, new_selection);
                s_browser_state.selected_index = new_selection;

                // 局部刷新：只重绘受影响的文件项
                int start_y = 80;
                int item_height = 50;
                int region_x = 20;  // 与文件项左边距一致
                int region_w = SCREEN_WIDTH - 40;  // 与文件项宽度一致
                int region_h = item_height;

                int old_y = start_y + old_selection * item_height;
                int new_y = start_y + new_selection * item_height;

                // 计算并集区域
                int refresh_y = old_y < new_y ? old_y : new_y;
                int refresh_bottom = (old_y > new_y ? old_y : new_y) + region_h;
                int refresh_h = refresh_bottom - refresh_y;

                // 先清除脏区，避免脏区累积
                display_clear_dirty();

                // 计算文件索引
                int file_idx_old = s_browser_state.current_page * s_browser_state.files_per_page + old_selection;
                int file_idx_new = s_browser_state.current_page * s_browser_state.files_per_page + new_selection;

                // 重绘旧焦点（恢复为非选中状态）
                ESP_LOGI(TAG, "Redrawing old item %d (deselected)", old_selection);
                display_clear_region(region_x, old_y, region_w, region_h, COLOR_WHITE);
                draw_single_file(old_selection, &s_browser_state.files[file_idx_old], false);
                display_clear_dirty();  // 清除本次绘制的脏区

                // 重绘新焦点（设置为选中状态）
                ESP_LOGI(TAG, "Redrawing new item %d (selected)", new_selection);
                display_clear_region(region_x, new_y, region_w, region_h, COLOR_WHITE);
                draw_single_file(new_selection, &s_browser_state.files[file_idx_new], true);

                // 标记脏区并刷新
                display_mark_dirty(region_x, refresh_y, region_w, refresh_h);
                display_refresh(REFRESH_MODE_PARTIAL);

                ESP_LOGI(TAG, "Focus update complete (partial refresh: y=%d h=%d)", refresh_y, refresh_h);
            }
            break;

        case BTN_VOLUME_DOWN:
            // 向下选择（选中下一个文件）
            if (s_browser_state.selected_index < items_on_current_page - 1) {
                int old_selection = s_browser_state.selected_index;
                int new_selection = old_selection + 1;

                ESP_LOGI(TAG, "Focus changed: %d -> %d", old_selection, new_selection);
                s_browser_state.selected_index = new_selection;

                // 局部刷新：只重绘受影响的文件项
                int start_y = 80;
                int item_height = 50;
                int region_x = 20;  // 与文件项左边距一致
                int region_w = SCREEN_WIDTH - 40;  // 与文件项宽度一致
                int region_h = item_height;

                int old_y = start_y + old_selection * item_height;
                int new_y = start_y + new_selection * item_height;

                // 计算并集区域
                int refresh_y = old_y < new_y ? old_y : new_y;
                int refresh_bottom = (old_y > new_y ? old_y : new_y) + region_h;
                int refresh_h = refresh_bottom - refresh_y;

                // 先清除脏区，避免脏区累积
                display_clear_dirty();

                // 计算文件索引
                int file_idx_old = s_browser_state.current_page * s_browser_state.files_per_page + old_selection;
                int file_idx_new = s_browser_state.current_page * s_browser_state.files_per_page + new_selection;

                // 重绘旧焦点（恢复为非选中状态）
                ESP_LOGI(TAG, "Redrawing old item %d (deselected)", old_selection);
                display_clear_region(region_x, old_y, region_w, region_h, COLOR_WHITE);
                draw_single_file(old_selection, &s_browser_state.files[file_idx_old], false);
                display_clear_dirty();  // 清除本次绘制的脏区

                // 重绘新焦点（设置为选中状态）
                ESP_LOGI(TAG, "Redrawing new item %d (selected)", new_selection);
                display_clear_region(region_x, new_y, region_w, region_h, COLOR_WHITE);
                draw_single_file(new_selection, &s_browser_state.files[file_idx_new], true);

                // 标记脏区并刷新
                display_mark_dirty(region_x, refresh_y, region_w, refresh_h);
                display_refresh(REFRESH_MODE_PARTIAL);

                ESP_LOGI(TAG, "Focus update complete (partial refresh: y=%d h=%d)", refresh_y, refresh_h);
            }
            break;

        case BTN_BACK:
            // 返回上级目录或首页
            navigate_to_parent_directory();
            break;

        case BTN_CONFIRM:
            // 打开选中的文件/目录
            open_selected_file();
            break;

        default:
            break;
    }
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void file_browser_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing file browser screen");

    g_file_browser_screen.name = "file_browser";
    g_file_browser_screen.user_data = NULL;
    g_file_browser_screen.on_show = on_show;
    g_file_browser_screen.on_hide = on_hide;
    g_file_browser_screen.on_draw = on_draw;
    g_file_browser_screen.on_event = on_event;
    g_file_browser_screen.is_visible = false;
    g_file_browser_screen.needs_redraw = false;
}

screen_t* file_browser_screen_get_instance(void)
{
    if (g_file_browser_screen.name == NULL) {
        file_browser_screen_init();
    }
    return &g_file_browser_screen;
}
