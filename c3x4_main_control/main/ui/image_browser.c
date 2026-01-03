/**
 * @file image_browser.c
 * @brief 图片浏览器实现
 *
 * 内存优化说明：
 * - 对于 ESP32-C3 有限内存，使用 LVGL 内置解码器直接加载图片
 * - 图片按屏幕尺寸显示，不需要额外缩放
 * - 使用 1-bit 灰度格式以减少内存占用
 */

#include "image_browser.h"
#include "../lvgl_driver.h"
#include "esp_log.h"
#include "font_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "IMAGE_BROWSER";

#define MAX_IMAGES 100
#define MAX_PATH_LEN 256

// 图片解码缓冲区大小（分块解码以减少内存占用）
#define DECODE_BUFFER_SIZE 8192

static image_browser_state_t g_browser = {0};
static TimerHandle_t s_slideshow_timer = NULL;

// 支持的图片扩展名
static const char *supported_extensions[] = {
    ".png", ".jpg", ".jpeg", ".bmp", ".gif", NULL
};

/**
 * @brief 检查文件是否是支持的图片格式
 */
image_format_t image_browser_get_image_format(const char *filename) {
    if (filename == NULL) {
        return IMAGE_FORMAT_UNKNOWN;
    }

    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        return IMAGE_FORMAT_UNKNOWN;
    }

    // 不区分大小写比较
    if (strcasecmp(ext, ".png") == 0) {
        return IMAGE_FORMAT_PNG;
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return IMAGE_FORMAT_JPEG;
    } else if (strcasecmp(ext, ".bmp") == 0) {
        return IMAGE_FORMAT_BMP;
    } else if (strcasecmp(ext, ".gif") == 0) {
        return IMAGE_FORMAT_GIF;
    }

    return IMAGE_FORMAT_UNKNOWN;
}

/**
 * @brief 加载图片到 LVGL（使用内置解码器，内存优化版本）
 *
 * 此函数使用 LVGL 内置的图片解码器直接从文件加载图片，
 * 避免将整个图片读入内存。
 */
static bool load_image_to_lvgl(lv_obj_t *image_obj, const char *file_path) {
    if (image_obj == NULL || file_path == NULL) {
        return false;
    }

    // 将 /sdcard/xxx 转换为 S:/xxx (LVGL 文件系统路径)
    const char *lvgl_path = file_path;
    char lvgl_path_buf[256];

    if (strncmp(file_path, "/sdcard/", 8) == 0) {
        snprintf(lvgl_path_buf, sizeof(lvgl_path_buf), "S:/%s", file_path + 8);
        lvgl_path = lvgl_path_buf;
    }

    ESP_LOGI(TAG, "Loading image via LVGL FS: %s -> %s", file_path, lvgl_path);

    // 使用 LVGL 内置解码器直接加载文件
    // LVGL 9.x 会自动处理解码和格式转换
    lv_image_set_src(image_obj, lvgl_path);

    // 检查图片是否加载成功
    lv_image_dsc_t *img_dsc = (lv_image_dsc_t *)lv_image_get_src(image_obj);
    if (img_dsc == NULL) {
        ESP_LOGE(TAG, "Failed to load image: %s (tried: %s)", file_path, lvgl_path);
        return false;
    }

    ESP_LOGI(TAG, "Image loaded: %s, size: %dx%d, cf:%d",
             lvgl_path, img_dsc->header.w, img_dsc->header.h, img_dsc->header.cf);

    return true;
}

/**
 * @brief 幻灯片播放定时器回调
 */
static void slideshow_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    // 切换到下一张图片
    if (!image_browser_next_image()) {
        // 如果已经是最后一张，回到第一张
        image_browser_show_image(0);
    }
}

bool image_browser_init(void) {
    ESP_LOGI(TAG, "Initializing image browser...");

    memset(&g_browser, 0, sizeof(image_browser_state_t));

    // 分配图片数组
    g_browser.images = calloc(MAX_IMAGES, sizeof(image_info_t));
    if (g_browser.images == NULL) {
        ESP_LOGE(TAG, "Failed to allocate image array");
        return false;
    }

    // 创建幻灯片播放定时器
    s_slideshow_timer = xTimerCreate("slideshow",
                                      pdMS_TO_TICKS(3000),  // 默认 3 秒
                                      pdFALSE,  // 不自动重载
                                      NULL,
                                      slideshow_timer_callback);
    if (s_slideshow_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create slideshow timer");
    }

    // 注意：不在这里扫描目录，由 screen_create 时传入实际目录
    ESP_LOGI(TAG, "Image browser initialized");
    return true;
}

int image_browser_scan_directory(const char *directory) {
    if (directory == NULL || g_browser.images == NULL) {
        ESP_LOGE(TAG, "Invalid parameters: directory=%p, images=%p", directory, g_browser.images);
        return 0;
    }

    ESP_LOGI(TAG, "Scanning directory for images: %s", directory);

    // 测试目录是否可以打开
    DIR *test_dir = opendir(directory);
    if (test_dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s (errno=%d)", directory, errno);
        return 0;
    }
    closedir(test_dir);
    ESP_LOGI(TAG, "Directory opened successfully");

    // 清理之前的扫描结果
    for (int i = 0; i < g_browser.image_count; i++) {
        if (g_browser.images[i].decoded_data != NULL) {
            free(g_browser.images[i].decoded_data);
        }
    }
    memset(g_browser.images, 0, MAX_IMAGES * sizeof(image_info_t));
    g_browser.image_count = 0;

    // 停止幻灯片播放
    image_browser_slideshow_stop();

    DIR *dir = opendir(directory);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", directory);
        return 0;
    }

    struct dirent *entry;
    int count = 0;
    int total_scanned = 0;

    while ((entry = readdir(dir)) != NULL && g_browser.image_count < MAX_IMAGES) {
        const char *name = entry->d_name;
        total_scanned++;

        // 跳过 "." 和 ".."
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // 调试：打印所有扫描到的文件
        ESP_LOGD(TAG, "Scanned file: %s (d_type=%d)", name, entry->d_type);

        // 检查是否是图片格式
        image_format_t format = image_browser_get_image_format(name);
        if (format == IMAGE_FORMAT_UNKNOWN) {
            continue;
        }

        // 构建完整路径
        char full_path[MAX_PATH_LEN];
        int len = snprintf(full_path, MAX_PATH_LEN - 1, "%s/%s", directory, name);
        if (len >= MAX_PATH_LEN - 1) {
            full_path[MAX_PATH_LEN - 1] = '\0';
        }

        // 检查是否是文件（非目录）
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            image_info_t *img = &g_browser.images[g_browser.image_count];
            strncpy(img->file_path, full_path, sizeof(img->file_path) - 1);
            img->file_path[sizeof(img->file_path) - 1] = '\0';
            img->format = format;

            const char *format_str[] = {"UNKNOWN", "PNG", "JPEG", "BMP", "GIF"};
            ESP_LOGI(TAG, "Found image: %s (%s)", name, format_str[format]);

            g_browser.image_count++;
            count++;
        }
    }

    closedir(dir);

    g_browser.current_index = 0;
    g_browser.total_count = count;

    ESP_LOGI(TAG, "Scan complete: %d files scanned, found %d images", total_scanned, count);
    return count;
}

bool image_browser_show_image(int index) {
    if (index < 0 || index >= g_browser.image_count || g_browser.image_obj == NULL) {
        ESP_LOGE(TAG, "Invalid image index: %d (count=%d)", index, g_browser.image_count);
        return false;
    }

    image_info_t *img = &g_browser.images[index];

    // 释放之前的图片数据
    if (img->decoded_data != NULL) {
        free(img->decoded_data);
        img->decoded_data = NULL;
    }

    // 使用 LVGL 内置解码器加载图片（内存优化）
    if (!load_image_to_lvgl(g_browser.image_obj, img->file_path)) {
        ESP_LOGE(TAG, "Failed to load image: %s", img->file_path);
        return false;
    }

    // 更新信息标签
    if (g_browser.info_label != NULL) {
        char info_text[64];
        const char *format_str[] = {"UNK", "PNG", "JPG", "BMP", "GIF"};
        snprintf(info_text, sizeof(info_text) - 1, "%d/%d - %s",
                 index + 1, g_browser.image_count, format_str[img->format]);
        info_text[sizeof(info_text) - 1] = '\0';
        lv_label_set_text(g_browser.info_label, info_text);
    }

    // 保存当前索引
    g_browser.current_index = index;

    // 触发刷新
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_trigger_render(NULL);
    lvgl_display_refresh();

    const char *filename = strrchr(img->file_path, '/');
    ESP_LOGI(TAG, "Showing image %d/%d: %s", index + 1, g_browser.image_count,
             filename ? filename + 1 : img->file_path);

    return true;
}

bool image_browser_prev_image(void) {
    if (g_browser.image_count <= 1) {
        return false;
    }

    int new_index = g_browser.current_index - 1;
    if (new_index < 0) {
        new_index = g_browser.image_count - 1;
    }

    return image_browser_show_image(new_index);
}

bool image_browser_next_image(void) {
    if (g_browser.image_count <= 1) {
        return false;
    }

    int new_index = g_browser.current_index + 1;
    if (new_index >= g_browser.image_count) {
        new_index = 0;
    }

    return image_browser_show_image(new_index);
}

int image_browser_get_current_index(void) {
    return g_browser.current_index;
}

int image_browser_get_total_count(void) {
    return g_browser.image_count;
}

void image_browser_slideshow_start(int interval_ms) {
    if (s_slideshow_timer == NULL || g_browser.image_count <= 1) {
        return;
    }

    ESP_LOGI(TAG, "Starting slideshow with interval: %d ms", interval_ms);

    // 停止之前的定时器
    xTimerStop(s_slideshow_timer, 0);

    // 设置新的间隔并启动
    xTimerChangePeriod(s_slideshow_timer, pdMS_TO_TICKS(interval_ms), 0);
    xTimerStart(s_slideshow_timer, 0);

    g_browser.is_playing = true;
}

void image_browser_slideshow_stop(void) {
    if (s_slideshow_timer != NULL) {
        xTimerStop(s_slideshow_timer, 0);
    }
    g_browser.is_playing = false;
}

void image_browser_cleanup(void) {
    ESP_LOGI(TAG, "Cleaning up image browser...");

    // 停止幻灯片播放
    image_browser_slideshow_stop();

    // 释放定时器
    if (s_slideshow_timer != NULL) {
        xTimerDelete(s_slideshow_timer, 0);
        s_slideshow_timer = NULL;
    }

    // 释放图片数据
    if (g_browser.images != NULL) {
        for (int i = 0; i < g_browser.image_count; i++) {
            if (g_browser.images[i].decoded_data != NULL) {
                free(g_browser.images[i].decoded_data);
            }
        }
        free(g_browser.images);
        g_browser.images = NULL;
    }

    g_browser.image_count = 0;
    g_browser.current_index = 0;
    g_browser.image_obj = NULL;
    g_browser.info_label = NULL;

    ESP_LOGI(TAG, "Image browser cleaned up");
}

/**
 * @brief 创建图片查看器屏幕
 * @param directory 要浏览的目录
 * @param start_index 起始图片索引
 * @param indev 输入设备
 */
void image_browser_screen_create(const char *directory, int start_index, lv_indev_t *indev) {
    ESP_LOGI(TAG, "Creating image viewer screen for: %s", directory);

    // 初始化图片浏览器（如果尚未初始化）
    static bool s_initialized = false;
    if (!s_initialized) {
        if (!image_browser_init()) {
            ESP_LOGE(TAG, "Failed to initialize image browser");
            return;
        }
        s_initialized = true;
    }

    // 扫描目录
    if (image_browser_scan_directory(directory) == 0) {
        ESP_LOGE(TAG, "No images found in directory");
        return;
    }

    // 如果有图片，显示第一张或指定的图片
    if (start_index >= 0 && start_index < g_browser.image_count) {
        g_browser.current_index = start_index;
    }

    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    // 设置背景为白色
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // ========================================
    // 图片显示区域（全屏无边框）
    // ========================================
    g_browser.container = lv_obj_create(screen);
    lv_obj_set_size(g_browser.container, 480, 800);
    lv_obj_align(g_browser.container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(g_browser.container, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_browser.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_browser.container, 0, 0);
    lv_obj_set_style_pad_all(g_browser.container, 0, 0);

    // 创建图片对象（全屏）
    g_browser.image_obj = lv_image_create(g_browser.container);
    lv_obj_set_size(g_browser.image_obj, 480, 800);
    lv_obj_align(g_browser.image_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(g_browser.image_obj, 0, 0);

    // ========================================
    // 信息标签（底部）
    // ========================================
    g_browser.info_label = lv_label_create(screen);
    lv_obj_set_style_text_font(g_browser.info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_browser.info_label, lv_color_black(), 0);
    lv_label_set_text(g_browser.info_label, "Loading...");
    lv_obj_align(g_browser.info_label, LV_ALIGN_BOTTOM_LEFT, 20, 40);

    // ========================================
    // 操作提示
    // ========================================
    lv_obj_t *hint1 = lv_label_create(screen);
    lv_label_set_text(hint1, "Vol+/-: Prev/Next");
    lv_obj_set_style_text_font(hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint1, lv_color_black(), 0);
    lv_obj_align(hint1, LV_ALIGN_BOTTOM_LEFT, 20, 780);

    lv_obj_t *hint2 = lv_label_create(screen);
    lv_label_set_text(hint2, "Confirm(3): Slideshow");
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint2, lv_color_black(), 0);
    lv_obj_align(hint2, LV_ALIGN_BOTTOM_LEFT, 20, 800);

    lv_obj_t *hint3 = lv_label_create(screen);
    lv_label_set_text(hint3, "Back(4): Return");
    lv_obj_set_style_text_font(hint3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint3, lv_color_black(), 0);
    lv_obj_align(hint3, LV_ALIGN_BOTTOM_LEFT, 20, 820);

    // 显示第一张图片
    image_browser_show_image(g_browser.current_index);

    // 全刷确保显示质量
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);
    lvgl_clear_framebuffer();
    lv_obj_invalidate(screen);

    for (int i = 0; i < 3; i++) {
        lvgl_trigger_render(NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (lvgl_is_refreshing()) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    lvgl_display_refresh();

    ESP_LOGI(TAG, "Image viewer screen created");
}
