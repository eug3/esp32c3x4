/**
 * @file font_loader.c
 * @brief LVGL 字体加载器实现
 *
 * lv_font_conv 生成的 .bin 文件格式：
 * - 文件头包含字体元数据（cmaps, glyph_dsc, glyph_bitmap 等）
 * - 后面跟随实际的位图数据
 *
 * 该实现将整个文件读取到内存，并设置正确的指针偏移。
 */

#include "font_loader.h"
#include "../lvgl_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "FONT_LOADER";

// 字体加载器全局状态
static font_loader_state_t g_font_loader = {0};

/**
 * @brief LVGL 字体文件头结构（对应 lv_font_fmt_txt_dsc_t）
 *
 * 注意：这是二进制文件中的布局，需要与 lv_font_conv 输出匹配
 */
typedef struct {
    // 指针字段（在加载时需要重新定位）
    uint32_t glyph_bitmap_offset;    // glyph_bitmap 相对于文件头的偏移
    uint32_t glyph_dsc_offset;       // glyph_dsc 相对于文件头的偏移
    uint32_t cmaps_offset;           // cmaps 相对于文件头的偏移
    uint32_t kern_dsc_offset;        // kern_dsc 相对于文件头的偏移（可能为 0）

    // 数值字段
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
    uint8_t stride;
} lv_font_file_header_t;

/**
 * @brief 从文件加载 LVGL 字体
 *
 * @param file_path 字体文件路径
 * @param font_name 输出字体名称
 * @param name_len 名称缓冲区大小
 * @return lv_font_t* 成功返回字体指针，失败返回 NULL
 */
static lv_font_t* load_font_file(const char *file_path, char *font_name, size_t name_len)
{
    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open font file: %s", file_path);
        return NULL;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < (long)sizeof(lv_font_file_header_t)) {
        ESP_LOGE(TAG, "Font file too small: %s (%ld bytes)", file_path, file_size);
        fclose(fp);
        return NULL;
    }

    ESP_LOGI(TAG, "Loading font file: %s (%ld bytes)", file_path, file_size);

    // 读取文件头
    lv_font_file_header_t file_header;
    size_t read_size = fread(&file_header, 1, sizeof(lv_font_file_header_t), fp);
    if (read_size != sizeof(lv_font_file_header_t)) {
        ESP_LOGE(TAG, "Failed to read font file header");
        fclose(fp);
        return NULL;
    }

    // 读取 cmap 数量来计算需要的总内存
    int cmap_num = file_header.cmap_num;
    ESP_LOGI(TAG, "Font bpp=%u, cmap_num=%d, kern_classes=%u, bitmap_format=%u",
             file_header.bpp, cmap_num, file_header.kern_classes, file_header.bitmap_format);

    // 分配内存：lv_font_t + 所有数据
    // 我们需要将整个文件加载到内存，然后设置指针
    uint8_t *font_buffer = (uint8_t *)malloc(file_size);
    if (font_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for font", file_size);
        fclose(fp);
        return NULL;
    }

    // 读取整个文件
    fseek(fp, 0, SEEK_SET);
    read_size = fread(font_buffer, 1, file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        ESP_LOGE(TAG, "Failed to read complete font file (got %zu of %ld bytes)", read_size, file_size);
        free(font_buffer);
        return NULL;
    }

    // 设置 lv_font_t 结构（在 buffer 开头）
    lv_font_t *font = (lv_font_t *)font_buffer;

    // 设置字体基本信息
    font->subpx = LV_FONT_SUBPX_NONE;
    font->line_height = 0;  // 将从字体数据中获取
    font->base_line = 0;
    font->dsc = NULL;  // 将设置为指向 fmt_txt_dsc
    font->user_data = font_buffer;  // 保存 buffer 指针以便稍后释放
    font->get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font->get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;

    // 设置 fmt_txt_dsc（跳过 lv_font_t）
    lv_font_fmt_txt_dsc_t *dsc = (lv_font_fmt_txt_dsc_t *)(font_buffer + sizeof(lv_font_t));

    // 从文件头复制数据到 dsc
    dsc->glyph_bitmap = (const uint8_t *)(font_buffer + file_header.glyph_bitmap_offset);
    dsc->glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *)(font_buffer + file_header.glyph_dsc_offset);
    dsc->cmaps = (const lv_font_fmt_txt_cmap_t *)(font_buffer + file_header.cmaps_offset);
    dsc->kern_dsc = file_header.kern_dsc_offset > 0
                   ? (const void *)(font_buffer + file_header.kern_dsc_offset)
                   : NULL;
    dsc->kern_scale = file_header.kern_scale;
    dsc->cmap_num = file_header.cmap_num;
    dsc->bpp = file_header.bpp;
    dsc->kern_classes = file_header.kern_classes;
    dsc->bitmap_format = file_header.bitmap_format;
    dsc->stride = file_header.stride;

    // 设置字体描述符
    font->dsc = dsc;

    // 从字体数据中获取行高（使用第一个字符的高度作为参考）
    if (dsc->glyph_dsc != NULL && dsc->cmaps != NULL && dsc->cmaps->range_length > 0) {
        // 尝试获取第一个 glyph 的高度
        lv_font_fmt_txt_glyph_dsc_t *first_glyph = (lv_font_fmt_txt_glyph_dsc_t *)&dsc->glyph_dsc[dsc->cmaps->glyph_id_start];
        font->line_height = first_glyph->box_h;
        font->base_line = -first_glyph->ofs_y;
    }

    // 提取字体名称（从文件路径）
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

    ESP_LOGI(TAG, "Font loaded successfully: %s, line_height=%d", font_name, font->line_height);

    return font;
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

    // 设置默认字体为 montserrat
    g_font_loader.default_font = (lv_font_t *)&lv_font_montserrat_14;
    g_font_loader.current_font = g_font_loader.default_font;

    // 扫描字体文件
    int font_count = font_loader_scan_fonts();
    ESP_LOGI(TAG, "Font loader initialized, found %d font files", font_count);

    return true;
}

int font_loader_scan_fonts(void)
{
    ESP_LOGI(TAG, "Scanning for fonts in: %s", g_font_loader.font_dir);

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

        // 保存字体信息（不立即加载）
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

        g_font_loader.font_count++;
        scan_count++;

        ESP_LOGI(TAG, "Found font: %s -> %s", name, full_path);
    }

    closedir(dir);

    ESP_LOGI(TAG, "Scan complete: %d fonts found", scan_count);
    return scan_count;
}

lv_font_t* font_load_from_file(const char *file_path, char *font_name, size_t name_len)
{
    return load_font_file(file_path, font_name, name_len);
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
    lv_font_t *font = load_font_file(info->file_path, loaded_name, sizeof(loaded_name));

    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to load font at index %d: %s", index, info->file_path);
        return NULL;
    }

    info->lv_font = font;
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

            // 如果引用计数为 0，释放内存
            if (g_font_loader.fonts[i].ref_count <= 0) {
                if (font->user_data != NULL) {
                    free(font->user_data);
                }
                g_font_loader.fonts[i].lv_font = NULL;
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
            if (g_font_loader.fonts[i].lv_font->user_data != NULL) {
                free(g_font_loader.fonts[i].lv_font->user_data);
            }
            g_font_loader.fonts[i].lv_font = NULL;
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
