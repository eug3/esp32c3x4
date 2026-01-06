/**
 * @file font_selector.c
 * @brief 字体选择器实现
 */

#include "font_selector.h"
#include "esp_log.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "FONT_SELECTOR";

// 字体目录列表
static const char *s_font_dirs[] = {
    "/sdcard/fonts",
    "/sdcard/字体",
    NULL
};

// 扫描到的字体
static font_info_t s_fonts[FONT_SELECTOR_MAX_FONTS];
static int s_font_count = 0;

static bool ends_with_ignore_case(const char *s, const char *suffix)
{
    if (s == NULL || suffix == NULL) {
        return false;
    }
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (su == 0 || sl < su) {
        return false;
    }
    const char *p = s + sl - su;
    for (size_t i = 0; i < su; i++) {
        char a = (char)tolower((unsigned char)p[i]);
        char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool is_valid_font_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    if (st.st_size <= 0) {
        return false;
    }
    // 检查是否为 .bin 文件
    if (!ends_with_ignore_case(path, ".bin")) {
        return false;
    }
    // 检查文件大小是否为 0x10000 的整数倍（XTEink字体格式）
    if ((st.st_size % 0x10000u) != 0) {
        return false;
    }
    return true;
}

static void extract_font_name(const char *path, char *name_out, size_t name_size)
{
    if (name_out == NULL || name_size == 0) {
        return;
    }

    // 获取文件名
    const char *base = strrchr(path, '/');
    const char *filename = (base != NULL) ? (base + 1) : path;

    // 去除扩展名
    const char *dot = strrchr(filename, '.');
    size_t name_len = (dot != NULL) ? (size_t)(dot - filename) : strlen(filename);

    // 复制名称
    if (name_len >= name_size) {
        name_len = name_size - 1;
    }
    memcpy(name_out, filename, name_len);
    name_out[name_len] = '\0';

    // 替换下划线和连字符为空格，使显示更友好
    for (size_t i = 0; i < name_len; i++) {
        if (name_out[i] == '_' || name_out[i] == '-') {
            name_out[i] = ' ';
        }
    }
}

static void parse_font_dimensions(const char *path, uint16_t *width, uint16_t *height)
{
    if (width == NULL || height == NULL) {
        return;
    }

    *width = 0;
    *height = 0;

    // 从文件名解析尺寸
    const char *base = strrchr(path, '/');
    const char *filename = (base != NULL) ? (base + 1) : path;

    // 尝试找 "WxH" 或 "W x H" 模式
    char *name_copy = strdup(filename);
    if (name_copy == NULL) {
        return;
    }

    // 去除扩展名
    char *dot = strrchr(name_copy, '.');
    if (dot != NULL) {
        *dot = '\0';
    }

    // 查找数字
    char *p = name_copy + strlen(name_copy) - 1;
    while (p > name_copy && (*p < '0' || *p > '9')) {
        p--;
    }

    if (p > name_copy) {
        // 找到末尾数字
        char *end = p;
        while (p > name_copy && (*(p - 1) >= '0' && *(p - 1) <= '9')) {
            p--;
        }

        uint32_t h = (uint32_t)strtoul(p, NULL, 10);

        // 往前找 'x' 或 'X'
        if (p > name_copy) {
            p--;
            while (p > name_copy && (*p == 'x' || *p == 'X' || *p == ' ')) {
                p--;
            }
            if (p >= name_copy) {
                char *num_start = p;
                while (num_start > name_copy && (*(num_start - 1) >= '0' && *(num_start - 1) <= '9')) {
                    num_start--;
                }
                uint32_t w = (uint32_t)strtoul(num_start, NULL, 10);
                if (w > 0 && w <= 255 && h > 0 && h <= 255) {
                    *width = (uint16_t)w;
                    *height = (uint16_t)h;
                }
            }
        }
    }

    free(name_copy);

    // 如果无法从文件名解析，尝试从文件大小推断
    if (*width == 0 || *height == 0) {
        struct stat st;
        if (stat(path, &st) == 0) {
            uint32_t file_size = (uint32_t)st.st_size;
            uint32_t char_byte = file_size / 0x10000u;

            // 常见的尺寸组合
            struct { uint16_t w; uint16_t h; uint32_t byte; } candidates[] = {
                { 8, 16, 16 },
                { 16, 12, 24 },
                { 16, 14, 28 },
                { 16, 16, 32 },
                { 16, 20, 40 },
                { 19, 25, 57 },  // 19x25 需要 3*25=75 字节/字符，但这里可能不匹配
                { 24, 24, 72 },
                { 32, 32, 128 },
            };

            for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
                if (candidates[i].byte == char_byte) {
                    *width = candidates[i].w;
                    *height = candidates[i].h;
                    break;
                }
            }
        }
    }
}

static void scan_directory(const char *dir_path)
{
    if (dir_path == NULL || s_font_count >= FONT_SELECTOR_MAX_FONTS) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        int err = errno;
        ESP_LOGW(TAG, "Cannot open directory: %s (errno=%d: %s)", dir_path, err, strerror(err));
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL && s_font_count < FONT_SELECTOR_MAX_FONTS) {
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        // 跳过 . 和 ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // 构建完整路径
        char fullpath[192];
        int n = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, name);
        if (n <= 0 || (size_t)n >= sizeof(fullpath)) {
            continue;
        }

        // 检查是否为有效字体文件
        if (!is_valid_font_file(fullpath)) {
            continue;
        }

        // 检查是否已存在（通过路径去重）
        bool exists = false;
        for (int i = 0; i < s_font_count; i++) {
            if (strcmp(s_fonts[i].path, fullpath) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        // 添加到列表
        font_info_t *font = &s_fonts[s_font_count];
        strncpy(font->path, fullpath, sizeof(font->path) - 1);
        font->path[sizeof(font->path) - 1] = '\0';

        extract_font_name(fullpath, font->name, sizeof(font->name));
        parse_font_dimensions(fullpath, &font->width, &font->height);

        struct stat st;
        stat(fullpath, &st);
        font->file_size = (uint32_t)st.st_size;

        ESP_LOGI(TAG, "Found font: %s (%s %dx%d)",
                 font->name, font->path, font->width, font->height);

        s_font_count++;
    }

    closedir(dir);
}

int font_selector_scan_fonts(font_info_t *fonts, int max_fonts)
{
    // 重置计数
    s_font_count = 0;

    // 扫描所有字体目录
    for (int i = 0; s_font_dirs[i] != NULL && s_font_count < FONT_SELECTOR_MAX_FONTS; i++) {
        scan_directory(s_font_dirs[i]);
    }

    // 如果没有找到字体，添加一个"默认"选项
    if (s_font_count == 0) {
        ESP_LOGW(TAG, "No fonts found in directories");
    }

    // 复制到输出数组
    int count = (s_font_count < max_fonts) ? s_font_count : max_fonts;
    if (fonts != NULL && count > 0) {
        memcpy(fonts, s_fonts, count * sizeof(font_info_t));
    }

    ESP_LOGI(TAG, "Font scan complete: %d fonts found", s_font_count);
    return s_font_count;
}

int font_selector_get_count(void)
{
    return s_font_count;
}

const font_info_t *font_selector_get_font(int index)
{
    if (index < 0 || index >= s_font_count) {
        return NULL;
    }
    return &s_fonts[index];
}

int font_selector_find_by_path(const char *path)
{
    if (path == NULL) {
        return -1;
    }
    for (int i = 0; i < s_font_count; i++) {
        if (strcmp(s_fonts[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

bool font_selector_is_valid_font(const char *path)
{
    return is_valid_font_file(path);
}
