/**
 * @file image_viewer_screen.c
 * @brief 图片查看器实现 - 支持 JPG/BMP 全屏显示 (PNG 待集成)
 */

#include "image_viewer_screen.h"
#include "display_engine.h"
#include "screen_manager.h"
#include "fonts.h"
#include "jpeg_helper.h"
#include "bmp_helper.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "IMAGE_VIEWER";
static screen_t g_image_viewer_screen = {0};

// 图片浏览器状态
static struct {
    char current_directory[256];    // 当前目录
    char target_file[256];          // 用户选择的目标文件名
    char current_file[256];         // 当前文件
    char **files;                   // 文件列表
    int file_count;                 // 文件总数
    int current_index;              // 当前文件索引
} s_viewer_state = {
    .files = NULL,
    .file_count = 0,
    .current_index = 0,
    .target_file = {0},
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_show(screen_t *screen);
static void on_hide(screen_t *screen);
static void on_draw(screen_t *screen);
static void on_event(screen_t *screen, button_t btn, button_event_t event);
static void free_file_list(void);
static bool scan_image_files(const char *directory);
static bool load_and_display_image(int index);
static const char* get_image_ext(const char *filename);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 获取图片文件扩展名
 */
static const char* get_image_ext(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext != NULL) {
        ext++;  // 跳过 '.'
    }
    return ext;
}

/**
 * @brief 释放文件列表
 */
static void free_file_list(void)
{
    if (s_viewer_state.files != NULL) {
        for (int i = 0; i < s_viewer_state.file_count; i++) {
            if (s_viewer_state.files[i] != NULL) {
                free(s_viewer_state.files[i]);
            }
        }
        free(s_viewer_state.files);
        s_viewer_state.files = NULL;
    }
    s_viewer_state.file_count = 0;
}

/**
 * @brief 扫描目录中的图片文件
 */
static bool scan_image_files(const char *directory)
{
    ESP_LOGI(TAG, "Scanning directory for images: %s", directory);

    // 释放旧的文件列表
    free_file_list();

    // 打开目录
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", directory);
        return false;
    }

    // 临时存储文件名
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
        // 跳过隐藏文件
        if (entry->d_name[0] == '.') {
            continue;
        }

        // 检查是否是图片文件
        const char *ext = get_image_ext(entry->d_name);
        if (ext != NULL &&
            (strcasecmp(ext, "jpg") == 0 ||
             strcasecmp(ext, "jpeg") == 0 ||
             strcasecmp(ext, "bmp") == 0 ||
             strcasecmp(ext, "png") == 0)) {

            // 分配内存存储文件名
            file_names[temp_count] = strdup(entry->d_name);
            if (file_names[temp_count] == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for file name");
                break;
            }
            temp_count++;
        }
    }
    closedir(dir);

    if (temp_count == 0) {
        ESP_LOGI(TAG, "No image files found in directory");
        heap_caps_free(file_names);
        return false;
    }

    // 分配文件列表内存
    s_viewer_state.files = (char **)heap_caps_malloc(temp_count * sizeof(char *), MALLOC_CAP_8BIT);
    if (s_viewer_state.files == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file list");
        for (int i = 0; i < temp_count; i++) {
            free(file_names[i]);
        }
        heap_caps_free(file_names);
        return false;
    }

    // 复制文件名
    for (int i = 0; i < temp_count; i++) {
        s_viewer_state.files[i] = file_names[i];
    }

    heap_caps_free(file_names);
    s_viewer_state.file_count = temp_count;

    ESP_LOGI(TAG, "Found %d image files", temp_count);

    return true;
}

/**
 * @brief 加载并显示指定索引的图片
 */
static bool load_and_display_image(int index)
{
    if (index < 0 || index >= s_viewer_state.file_count) {
        ESP_LOGE(TAG, "Invalid image index: %d (total: %d)", index, s_viewer_state.file_count);
        return false;
    }

    if (s_viewer_state.files[index] == NULL) {
        ESP_LOGE(TAG, "File name at index %d is NULL", index);
        return false;
    }

    // 构建完整文件路径
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s",
             s_viewer_state.current_directory,
             s_viewer_state.files[index]);

    ESP_LOGI(TAG, "Loading image: %s", full_path);
    strncpy(s_viewer_state.current_file, s_viewer_state.files[index],
            sizeof(s_viewer_state.current_file) - 1);
    s_viewer_state.current_file[sizeof(s_viewer_state.current_file) - 1] = '\0';

    // 打开文件
    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        return false;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ESP_LOGI(TAG, "Image file size: %ld bytes", file_size);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        fclose(f);
        return false;
    }

    // 分配内存读取文件
    uint8_t *image_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (image_data == NULL) {
        // 尝试使用内部内存
        image_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (image_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for image data (%ld bytes)", file_size);
            fclose(f);
            return false;
        }
    }

    size_t read_size = fread(image_data, 1, file_size, f);
    fclose(f);

    if (read_size != file_size) {
        ESP_LOGE(TAG, "Failed to read complete file (read: %d, expected: %ld)",
                 (int)read_size, file_size);
        heap_caps_free(image_data);
        return false;
    }

    // 清屏
    display_clear(COLOR_WHITE);

    // 根据文件扩展名选择解码方式
    const char *ext = get_image_ext(s_viewer_state.current_file);
    bool success = false;

    if (ext != NULL && (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)) {
        // 使用 JPEG 解码器
        ESP_LOGI(TAG, "Decoding JPEG image...");
        success = jpeg_helper_render_fullscreen(image_data, read_size);
    } else if (ext != NULL && strcasecmp(ext, "bmp") == 0) {
        // 使用 BMP 解码器
        ESP_LOGI(TAG, "Decoding BMP image...");
        success = bmp_helper_render_fullscreen(image_data, read_size);
    } else if (ext != NULL && strcasecmp(ext, "png") == 0) {
        // PNG 格式说明: 需要集成 PNGdec 库
        // PNGdec 是一个轻量级 PNG 解码器,适用于嵌入式系统
        // 可以从 https://github.com/bitbank2/PNGdec 获取
        // 集成步骤:
        // 1. 下载 PNGdec.h 和 PNGdec.cpp 到项目
        // 2. 实现 png_helper.h/c 类似 jpeg_helper
        // 3. 在此调用 png_helper_render_fullscreen()
        ESP_LOGE(TAG, "PNG format: 需要集成 PNGdec 库");
        display_draw_text_menu(20, SCREEN_HEIGHT / 2 - 20, "PNG format",
                               COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, SCREEN_HEIGHT / 2 + 20, "需要PNGdec库支持",
                               COLOR_BLACK, COLOR_WHITE);
    } else {
        // 其他格式暂不支持
        ESP_LOGE(TAG, "Unsupported image format: %s", ext ? ext : "unknown");
        display_draw_text_menu(20, SCREEN_HEIGHT / 2, "Format not supported",
                               COLOR_BLACK, COLOR_WHITE);
    }

    heap_caps_free(image_data);

    if (!success) {
        ESP_LOGE(TAG, "Failed to decode/display image");
        return false;
    }

    ESP_LOGI(TAG, "Image displayed successfully");
    return true;
}

/**********************
 * SCREEN CALLBACKS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Image viewer shown");

    // 获取完整文件路径
    const char *file_path = (const char *)screen->user_data;
    if (file_path == NULL) {
        ESP_LOGE(TAG, "No file path specified");
        file_path = "/sdcard/壁纸/地铁猫.jpg";  // 默认文件
    }

    ESP_LOGI(TAG, "Opening file: %s", file_path);

    // 从完整路径中提取目录和文件名
    const char *last_slash = strrchr(file_path, '/');
    if (last_slash == NULL) {
        ESP_LOGE(TAG, "Invalid file path: %s", file_path);
        return;
    }

    // 提取目录路径
    size_t dir_len = last_slash - file_path;
    if (dir_len >= sizeof(s_viewer_state.current_directory)) {
        dir_len = sizeof(s_viewer_state.current_directory) - 1;
    }
    strncpy(s_viewer_state.current_directory, file_path, dir_len);
    s_viewer_state.current_directory[dir_len] = '\0';

    // 提取文件名
    const char *filename = last_slash + 1;
    strncpy(s_viewer_state.target_file, filename, sizeof(s_viewer_state.target_file) - 1);
    s_viewer_state.target_file[sizeof(s_viewer_state.target_file) - 1] = '\0';

    ESP_LOGI(TAG, "Directory: %s, Target file: %s", s_viewer_state.current_directory, s_viewer_state.target_file);

    // 扫描图片文件
    if (!scan_image_files(s_viewer_state.current_directory)) {
        ESP_LOGW(TAG, "No images found, showing placeholder");

        // 显示占位符
        display_clear(COLOR_WHITE);
        display_draw_text_menu(20, 20, "No Images", COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, 100, "No supported image", COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, 150, "files in directory", COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, SCREEN_HEIGHT - 60, "返回: 返回", COLOR_BLACK, COLOR_WHITE);

        return;
    }

    // 查找目标文件在列表中的索引
    int target_index = 0;
    for (int i = 0; i < s_viewer_state.file_count; i++) {
        if (strcasecmp(s_viewer_state.files[i], s_viewer_state.target_file) == 0) {
            target_index = i;
            ESP_LOGI(TAG, "Found target file at index %d", target_index);
            break;
        }
    }

    // 显示目标图片
    s_viewer_state.current_index = target_index;
    load_and_display_image(target_index);

    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Image viewer hidden");

    // 释放文件列表
    free_file_list();
}

static void on_draw(screen_t *screen)
{
    // 图片显示在 on_show 中完成
    // 这里只需要显示提示信息
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    switch (btn) {
        case BTN_LEFT:
            // 上一张图片
            if (s_viewer_state.file_count > 0) {
                s_viewer_state.current_index--;
                if (s_viewer_state.current_index < 0) {
                    s_viewer_state.current_index = s_viewer_state.file_count - 1;  // 循环到最后一张
                }
                ESP_LOGI(TAG, "Previous image: %d/%d",
                         s_viewer_state.current_index + 1, s_viewer_state.file_count);
                load_and_display_image(s_viewer_state.current_index);
                display_refresh(REFRESH_MODE_FULL);
            }
            break;

        case BTN_RIGHT:
            // 下一张图片
            if (s_viewer_state.file_count > 0) {
                s_viewer_state.current_index++;
                if (s_viewer_state.current_index >= s_viewer_state.file_count) {
                    s_viewer_state.current_index = 0;  // 循环到第一张
                }
                ESP_LOGI(TAG, "Next image: %d/%d",
                         s_viewer_state.current_index + 1, s_viewer_state.file_count);
                load_and_display_image(s_viewer_state.current_index);
                display_refresh(REFRESH_MODE_FULL);
            }
            break;

        case BTN_BACK:
            // 返回
            screen_manager_back();
            break;

        default:
            break;
    }
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void image_viewer_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing image viewer screen");

    g_image_viewer_screen.name = "image_viewer";
    g_image_viewer_screen.user_data = NULL;
    g_image_viewer_screen.on_show = on_show;
    g_image_viewer_screen.on_hide = on_hide;
    g_image_viewer_screen.on_draw = on_draw;
    g_image_viewer_screen.on_event = on_event;
    g_image_viewer_screen.is_visible = false;
    g_image_viewer_screen.needs_redraw = false;
}

screen_t* image_viewer_screen_get_instance(void)
{
    if (g_image_viewer_screen.name == NULL) {
        image_viewer_screen_init();
    }
    return &g_image_viewer_screen;
}
