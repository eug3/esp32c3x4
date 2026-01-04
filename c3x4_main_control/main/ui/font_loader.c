/**
 * @file font_loader.c
 * @brief XTEink 字体加载器实现
 *
 * 只支持 XTEinkFontBinary 格式的字体文件
 */

#include "font_loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "FONT_LOADER";

// 字体加载器全局状态
static font_loader_state_t g_font_loader = {0};

// 默认字体（当没有 SD 卡字体时使用）
static lv_font_t *s_default_font = NULL;

/**
 * @brief 检查文件是否为有效的 XTEink 字体文件
 */
static bool is_xt_eink_font(const char *file_path)
{
    return xt_eink_font_is_valid(file_path);
}

bool font_loader_init(const char *font_dir)
{
    if (font_dir == NULL) {
        ESP_LOGE(TAG, "Font directory path is NULL");
        return false;
    }

    ESP_LOGI(TAG, "Initializing font loader with directory: %s", font_dir);

    // 清零状态
    memset(&g_font_loader, 0, sizeof(font_loader_state_t));

    // 保存字体目录路径
    strncpy(g_font_loader.font_dir, font_dir, sizeof(g_font_loader.font_dir) - 1);
    g_font_loader.font_dir[sizeof(g_font_loader.font_dir) - 1] = '\0';

    // 设置默认字体为 montserrat_14
    s_default_font = (lv_font_t *)&lv_font_montserrat_14;
    g_font_loader.default_font = s_default_font;
    g_font_loader.current_font = s_default_font;

    // 扫描字体文件
    int font_count = font_loader_scan_fonts();
    ESP_LOGI(TAG, "Font loader initialized, found %d XTEink font(s)", font_count);

    // 尝试加载默认字体 msyh-14.bin
    const char *default_font_path = "/fonts/msyh-14.bin";
    FILE *fp = fopen(default_font_path, "rb");
    if (fp != NULL) {
        fclose(fp);
        ESP_LOGI(TAG, "Loading default font: %s", default_font_path);
        char font_name[64];
        lv_font_t *default_font = font_load_from_file(default_font_path, font_name, sizeof(font_name));
        if (default_font != NULL) {
            g_font_loader.default_font = default_font;
            g_font_loader.current_font = default_font;
            ESP_LOGI(TAG, "Default font loaded: %s", font_name);
        } else {
            ESP_LOGW(TAG, "Failed to load default font, using montserrat_14");
        }
    }

    return true;
}

int font_loader_scan_fonts(void)
{
    ESP_LOGI(TAG, "Scanning for fonts in: %s", g_font_loader.font_dir);

    if (g_font_loader.font_count > 0) {
        ESP_LOGW(TAG, "Fonts already scanned (%d), skipping rescan", g_font_loader.font_count);
        return g_font_loader.font_count;
    }

    return font_loader_rescan_fonts();
}

int font_loader_rescan_fonts(void)
{
    ESP_LOGI(TAG, "Rescanning fonts in: %s", g_font_loader.font_dir);

    // 清除之前的扫描结果
    g_font_loader.font_count = 0;
    memset(g_font_loader.fonts, 0, sizeof(g_font_loader.fonts));

    DIR *dir = opendir(g_font_loader.font_dir);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open font directory: %s", g_font_loader.font_dir);
        return 0;
    }

    struct dirent *entry;
    int scan_count = 0;

    while ((entry = readdir(dir)) != NULL && g_font_loader.font_count < MAX_FONTS) {
        const char *name = entry->d_name;

        // 跳过 "." 和 ".."
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // 检查文件扩展名
        const char *dot = strrchr(name, '.');
        if (dot == NULL || strcasecmp(dot, ".bin") != 0) {
            continue;
        }

        // 构建完整路径
        char full_path[256];
        int len = snprintf(full_path, sizeof(full_path) - 1, "%s/%s", g_font_loader.font_dir, name);
        if (len >= (int)(sizeof(full_path) - 1)) {
            ESP_LOGW(TAG, "Font path truncated: %s/%s", g_font_loader.font_dir, name);
            continue;
        }

        // 验证是否为 XTEink 字体格式
        if (!is_xt_eink_font(full_path)) {
            ESP_LOGD(TAG, "Skipping non-XTEink font: %s", name);
            continue;
        }

        // 保存字体信息
        font_info_t *info = &g_font_loader.fonts[g_font_loader.font_count];
        memset(info, 0, sizeof(font_info_t));

        // 提取字体名称（去掉 .bin 扩展名）
        size_t name_len = dot - name;
        if (name_len >= MAX_FONT_NAME_LEN) {
            name_len = MAX_FONT_NAME_LEN - 1;
        }
        strncpy(info->name, name, name_len);
        info->name[name_len] = '\0';

        strncpy(info->file_path, full_path, sizeof(info->file_path) - 1);
        info->file_path[sizeof(info->file_path) - 1] = '\0';
        info->is_loaded = false;
        info->lv_font = NULL;
        info->xt_font = NULL;

        g_font_loader.font_count++;
        scan_count++;

        ESP_LOGI(TAG, "Found font [%d]: %s", g_font_loader.font_count - 1, info->name);
    }

    closedir(dir);

    ESP_LOGI(TAG, "Scan complete: %d XTEink fonts found", scan_count);
    return scan_count;
}

lv_font_t* font_load_from_file(const char *file_path, char *font_name, size_t name_len)
{
    if (file_path == NULL) {
        return NULL;
    }

    // 验证是否为 XTEink 字体
    if (!is_xt_eink_font(file_path)) {
        ESP_LOGE(TAG, "Not a valid XTEink font: %s", file_path);
        return NULL;
    }

    // 使用 XTEink 字体加载器
    lv_font_t *font = xt_eink_font_create(file_path);
    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to create XTEink font: %s", file_path);
        return NULL;
    }

    // 提取字体名称
    if (font_name != NULL && name_len > 0) {
        const char *last_slash = strrchr(file_path, '/');
        const char *basename = (last_slash != NULL) ? (last_slash + 1) : file_path;
        const char *dot = strrchr(basename, '.');
        size_t name_length = (dot != NULL) ? (dot - basename) : strlen(basename);
        if (name_length >= name_len) {
            name_length = name_len - 1;
        }
        strncpy(font_name, basename, name_length);
        font_name[name_length] = '\0';
    }

    ESP_LOGI(TAG, "Font loaded: %s", font_name ? font_name : "unknown");
    return font;
}

lv_font_t* font_load_by_index(int index)
{
    if (index < 0 || index >= g_font_loader.font_count) {
        ESP_LOGE(TAG, "Invalid font index: %d (count=%d)", index, g_font_loader.font_count);
        return NULL;
    }

    font_info_t *info = &g_font_loader.fonts[index];

    // 如果已经加载，直接返回
    if (info->is_loaded && info->lv_font != NULL) {
        info->ref_count++;
        ESP_LOGI(TAG, "Font already loaded: %s (ref_count=%d)", info->name, info->ref_count);
        return info->lv_font;
    }

    // 加载字体
    char loaded_name[MAX_FONT_NAME_LEN];
    lv_font_t *font = font_load_from_file(info->file_path, loaded_name, sizeof(loaded_name));

    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to load font at index %d: %s", index, info->file_path);
        return NULL;
    }

    info->lv_font = font;
    info->xt_font = (xt_eink_font_t *)((xt_eink_lv_font_t *)font)->ctx;
    info->is_loaded = true;
    info->ref_count = 1;

    ESP_LOGI(TAG, "Font loaded by index %d: %s", index, loaded_name);
    return font;
}

const font_info_t* font_loader_get_font_list(void)
{
    return g_font_loader.fonts;
}

int font_loader_get_font_count(void)
{
    return g_font_loader.font_count;
}

void font_loader_set_current_font(lv_font_t *font)
{
    g_font_loader.current_font = (font != NULL) ? font : g_font_loader.default_font;
    ESP_LOGI(TAG, "Current font set to: %p", g_font_loader.current_font);
}

lv_font_t* font_loader_get_current_font(void)
{
    return g_font_loader.current_font;
}

lv_font_t* font_loader_get_default_font(void)
{
    return g_font_loader.default_font;
}

const font_info_t* font_loader_find_font_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_font_loader.font_count; i++) {
        if (strcmp(g_font_loader.fonts[i].name, name) == 0) {
            return &g_font_loader.fonts[i];
        }
    }

    return NULL;
}

void font_loader_unload_font(lv_font_t *font)
{
    if (font == NULL) {
        return;
    }

    // 查找字体并减少引用计数
    for (int i = 0; i < g_font_loader.font_count; i++) {
        if (g_font_loader.fonts[i].lv_font == font) {
            g_font_loader.fonts[i].ref_count--;
            ESP_LOGI(TAG, "Font ref_count decreased: %s (now %d)",
                     g_font_loader.fonts[i].name, g_font_loader.fonts[i].ref_count);

            if (g_font_loader.fonts[i].ref_count <= 0) {
                if (g_font_loader.fonts[i].lv_font != NULL) {
                    xt_eink_font_destroy(g_font_loader.fonts[i].lv_font);
                }
                g_font_loader.fonts[i].lv_font = NULL;
                g_font_loader.fonts[i].xt_font = NULL;
                g_font_loader.fonts[i].is_loaded = false;
                g_font_loader.fonts[i].ref_count = 0;
                ESP_LOGI(TAG, "Font unloaded: %s", g_font_loader.fonts[i].name);
            }
            return;
        }
    }

    ESP_LOGW(TAG, "Font not found in loader list: %p", font);
}

void font_loader_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up font loader...");

    for (int i = 0; i < g_font_loader.font_count; i++) {
        if (g_font_loader.fonts[i].lv_font != NULL) {
            xt_eink_font_destroy(g_font_loader.fonts[i].lv_font);
            g_font_loader.fonts[i].lv_font = NULL;
            g_font_loader.fonts[i].xt_font = NULL;
            g_font_loader.fonts[i].is_loaded = false;
            g_font_loader.fonts[i].ref_count = 0;
        }
    }

    memset(&g_font_loader, 0, sizeof(font_loader_state_t));
    ESP_LOGI(TAG, "Font loader cleanup complete");
}

font_loader_state_t* font_loader_get_state(void)
{
    return &g_font_loader;
}
