/**
 * @file xt_eink_font.c
 * @brief XTEinkFontBinary 字体解析器实现
 */

#include "xt_eink_font.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "XT_EINK_FONT";

// 字形位图缓冲区（临时缓存，用于从文件读取）
static uint8_t *g_glyph_buffer = NULL;
static size_t g_glyph_buffer_size = 0;

// LVGL 字形描述符（静态分配，避免频繁分配）
static lv_font_glyph_dsc_t g_glyph_dsc;

/**
 * @brief 确保字形缓冲区足够大
 */
static bool ensure_glyph_buffer(size_t size)
{
    if (g_glyph_buffer == NULL || size > g_glyph_buffer_size) {
        if (g_glyph_buffer != NULL) {
            free(g_glyph_buffer);
        }
        g_glyph_buffer = (uint8_t *)malloc(size);
        if (g_glyph_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate glyph buffer (%u bytes)", size);
            return false;
        }
        g_glyph_buffer_size = size;
    }
    return true;
}

/**
 * @brief 从缓存获取字形
 */
static xt_eink_glyph_cache_t *get_cached_glyph(xt_eink_font_t *font, uint32_t unicode)
{
    for (int i = 0; i < XT_EINK_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].cached && font->cache[i].unicode == unicode) {
            font->cache_access_counter++;
            font->cache[i].last_access = font->cache_access_counter;
            font->cache_hit++;
            return &font->cache[i];
        }
    }
    font->cache_miss++;
    return NULL;
}

/**
 * @brief 将字形添加到缓存（LRU）
 */
static void cache_glyph(xt_eink_font_t *font, uint32_t unicode, uint8_t *bitmap, size_t size)
{
    // 查找最久未使用的缓存项
    uint32_t oldest_access = UINT32_MAX;
    int oldest_idx = -1;

    for (int i = 0; i < XT_EINK_GLYPH_CACHE_SIZE; i++) {
        if (!font->cache[i].cached) {
            oldest_idx = i;
            break;
        }
        if (font->cache[i].last_access < oldest_access) {
            oldest_access = font->cache[i].last_access;
            oldest_idx = i;
        }
    }

    if (oldest_idx < 0) {
        return;  // 缓存已满
    }

    // 释放旧的缓存项
    xt_eink_glyph_cache_t *item = &font->cache[oldest_idx];
    if (item->cached && item->bitmap != NULL) {
        free(item->bitmap);
    }

    // 添加新的缓存项
    item->bitmap = (uint8_t *)malloc(size);
    if (item->bitmap != NULL) {
        memcpy(item->bitmap, bitmap, size);
        item->unicode = unicode;
        item->bitmap_size = size;
        item->cached = true;
        font->cache_access_counter++;
        item->last_access = font->cache_access_counter;
    }
}

/**
 * @brief 从文件读取字形位图
 */
static bool read_glyph_from_file(xt_eink_font_t *font, uint32_t unicode, uint8_t *bitmap)
{
    // 检查 Unicode 范围
    if (unicode < font->header.first_char || unicode > font->header.last_char) {
        return false;
    }

    // 计算字形在文件中的偏移
    // 字形数据从文件头之后开始
    uint32_t header_size = sizeof(xt_eink_font_header_t);
    uint32_t glyph_index = unicode - font->header.first_char;
    uint32_t offset = header_size + glyph_index * font->header.glyph_size;

    if (offset + font->header.glyph_size > font->file_size) {
        ESP_LOGE(TAG, "Glyph offset out of range: offset=%u, size=%u, file_size=%u",
                 offset, font->header.glyph_size, font->file_size);
        return false;
    }

    // 定位并读取
    if (fseek(font->fp, offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek to glyph offset %u", offset);
        return false;
    }

    size_t read_size = fread(bitmap, 1, font->header.glyph_size, font->fp);
    if (read_size != font->header.glyph_size) {
        ESP_LOGE(TAG, "Failed to read glyph: expected=%u, got=%zu",
                 font->header.glyph_size, read_size);
        return false;
    }

    return true;
}

xt_eink_font_t *xt_eink_font_open(const char *path)
{
    if (path == NULL) {
        ESP_LOGE(TAG, "Font path is NULL");
        return NULL;
    }

    // 检查文件是否存在
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open font file: %s", path);
        return NULL;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < (long)sizeof(xt_eink_font_header_t)) {
        ESP_LOGE(TAG, "Font file too small: %s (%ld bytes)", path, file_size);
        fclose(fp);
        return NULL;
    }

    // 读取文件头
    xt_eink_font_header_t header;
    size_t read_size = fread(&header, 1, sizeof(header), fp);
    if (read_size != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to read font header");
        fclose(fp);
        return NULL;
    }

    // 验证魔数
    if (header.magic != XT_EINK_MAGIC) {
        ESP_LOGE(TAG, "Invalid font format: magic=0x%08X (expected 0x%08X)",
                 header.magic, XT_EINK_MAGIC);
        fclose(fp);
        return NULL;
    }

    // 验证版本
    if (header.version != XT_EINK_VERSION) {
        ESP_LOGE(TAG, "Unsupported font version: %u (expected %u)",
                 header.version, XT_EINK_VERSION);
        fclose(fp);
        return NULL;
    }

    // 验证 bpp
    if (header.bpp != 1) {
        ESP_LOGE(TAG, "Unsupported bpp: %u (only 1-bit supported)", header.bpp);
        fclose(fp);
        return NULL;
    }

    // 分配上下文
    xt_eink_font_t *font = (xt_eink_font_t *)malloc(sizeof(xt_eink_font_t));
    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to allocate font context");
        fclose(fp);
        return NULL;
    }

    memset(font, 0, sizeof(xt_eink_font_t));

    // 初始化上下文
    strncpy(font->file_path, path, sizeof(font->file_path) - 1);
    font->fp = fp;
    font->file_size = (uint32_t)file_size;
    font->header = header;
    font->width = header.width;
    font->height = header.height;
    font->glyph_size = header.glyph_size;
    font->line_height = header.height;

    ESP_LOGI(TAG, "Font opened: %s", path);
    ESP_LOGI(TAG, "  Size: %dx%d, bpp=%u", header.width, header.height, header.bpp);
    ESP_LOGI(TAG, "  Chars: U+%04X - U+%04X (%u chars)",
             header.first_char, header.last_char, header.char_count);
    ESP_LOGI(TAG, "  Glyph size: %u bytes", header.glyph_size);

    return font;
}

void xt_eink_font_close(xt_eink_font_t *font)
{
    if (font == NULL) {
        return;
    }

    // 清除缓存
    xt_eink_font_clear_cache(font);

    // 关闭文件
    if (font->fp != NULL) {
        fclose(font->fp);
        font->fp = NULL;
    }

    free(font);
    ESP_LOGI(TAG, "Font closed");
}

const lv_font_glyph_dsc_t *xt_eink_font_get_glyph_dsc(xt_eink_font_t *font,
                                                       uint32_t unicode,
                                                       uint32_t font_height)
{
    if (font == NULL || font->fp == NULL) {
        return NULL;
    }

    // 检查 Unicode 范围
    if (unicode < font->header.first_char || unicode > font->header.last_char) {
        return NULL;
    }

    // 检查字体高度是否匹配
    if (font_height != 0 && font_height != font->height) {
        ESP_LOGW(TAG, "Font height mismatch: requested=%u, actual=%u",
                 font_height, font->height);
    }

    // 设置字形描述符
    memset(&g_glyph_dsc, 0, sizeof(g_glyph_dsc));
    g_glyph_dsc.adv_w = font->width;           // 字形宽度
    g_glyph_dsc.box_w = font->width;           // 字形框宽度
    g_glyph_dsc.box_h = font->height;          // 字形框高度
    g_glyph_dsc.ofs_x = 0;                     // X 偏移
    g_glyph_dsc.ofs_y = 0;                     // Y 偏移（对于 1-bit 位图，从基线开始）
    g_glyph_dsc.bpp = font->header.bpp;        // 每像素位数

    return &g_glyph_dsc;
}

const uint8_t *xt_eink_font_get_bitmap(xt_eink_font_t *font, uint32_t unicode)
{
    if (font == NULL || font->fp == NULL) {
        return NULL;
    }

    // 检查缓存
    xt_eink_glyph_cache_t *cached = get_cached_glyph(font, unicode);
    if (cached != NULL) {
        return cached->bitmap;
    }

    // 确保缓冲区足够
    if (!ensure_glyph_buffer(font->glyph_size)) {
        return NULL;
    }

    // 从文件读取字形
    if (!read_glyph_from_file(font, unicode, g_glyph_buffer)) {
        return NULL;
    }

    // 添加到缓存
    cache_glyph(font, unicode, g_glyph_buffer, font->glyph_size);

    return g_glyph_buffer;
}

lv_font_t *xt_eink_font_create(const char *path)
{
    xt_eink_font_t *ctx = xt_eink_font_open(path);
    if (ctx == NULL) {
        return NULL;
    }

    // 分配 LVGL 字体包装器
    xt_eink_lv_font_t *font = (xt_eink_lv_font_t *)malloc(sizeof(xt_eink_lv_font_t));
    if (font == NULL) {
        xt_eink_font_close(ctx);
        return NULL;
    }

    // 设置 LVGL 字体回调
    font->base.get_glyph_dsc = xt_eink_font_get_glyph_dsc_cb;
    font->base.get_glyph_bitmap = xt_eink_font_get_glyph_bitmap_cb;
    font->base.subpx = LV_FONT_SUBPX_NONE;
    font->base.line_height = ctx->line_height;
    font->base.base_line = 0;
    font->base.dsc = ctx;  // 保存上下文指针
    font->base.user_data = font;  // 保存包装器指针
    font->ctx = ctx;
    font->ref_count = 1;

    ESP_LOGI(TAG, "LVGL font created from: %s", path);
    return (lv_font_t *)font;
}

void xt_eink_font_destroy(lv_font_t *lv_font)
{
    if (lv_font == NULL) {
        return;
    }

    xt_eink_lv_font_t *font = (xt_eink_lv_font_t *)lv_font;
    if (font->ctx != NULL) {
        xt_eink_font_close(font->ctx);
    }
    free(font);
    ESP_LOGI(TAG, "LVGL font destroyed");
}

void xt_eink_font_get_info(xt_eink_font_t *font, char *buffer, size_t buffer_size)
{
    if (font == NULL || buffer == NULL) {
        return;
    }

    snprintf(buffer, buffer_size,
             "XTEink Font\n"
             "  Path: %s\n"
             "  Size: %dx%d\n"
             "  Chars: %u (U+%04X - U+%04X)\n"
             "  Glyph size: %u bytes\n"
             "  Cache: %u/%u (hit=%u, miss=%u)",
             font->file_path,
             font->width, font->height,
             font->header.char_count,
             font->header.first_char, font->header.last_char,
             font->header.glyph_size,
             font->cache_hit + font->cache_miss > 0
                ? (font->cache_hit * 100) / (font->cache_hit + font->cache_miss)
                : 0,
             XT_EINK_GLYPH_CACHE_SIZE,
             font->cache_hit, font->cache_miss);
}

bool xt_eink_font_is_valid(const char *path)
{
    if (path == NULL) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }

    xt_eink_font_header_t header;
    bool valid = false;

    if (fread(&header, 1, sizeof(header), fp) == sizeof(header)) {
        valid = (header.magic == XT_EINK_MAGIC && header.version == XT_EINK_VERSION);
    }

    fclose(fp);
    return valid;
}

void xt_eink_font_get_cache_stats(xt_eink_font_t *font, uint32_t *hit, uint32_t *miss)
{
    if (font == NULL) {
        return;
    }
    if (hit) *hit = font->cache_hit;
    if (miss) *miss = font->cache_miss;
}

void xt_eink_font_clear_cache(xt_eink_font_t *font)
{
    if (font == NULL) {
        return;
    }

    for (int i = 0; i < XT_EINK_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].cached && font->cache[i].bitmap != NULL) {
            free(font->cache[i].bitmap);
            font->cache[i].bitmap = NULL;
            font->cache[i].cached = false;
        }
    }
    font->cache_hit = 0;
    font->cache_miss = 0;
}

/**
 * @brief LVGL 字形描述符回调
 */
const lv_font_glyph_dsc_t *xt_eink_font_get_glyph_dsc_cb(const lv_font_t *font,
                                                          uint32_t unicode,
                                                          uint32_t font_height)
{
    xt_eink_lv_font_t *xt_font = (xt_eink_lv_font_t *)font;
    if (xt_font->ctx == NULL) {
        return NULL;
    }
    return xt_eink_font_get_glyph_dsc(xt_font->ctx, unicode, font_height);
}

/**
 * @brief LVGL 字形位图回调
 */
const uint8_t *xt_eink_font_get_glyph_bitmap_cb(const lv_font_t *font, uint32_t unicode)
{
    xt_eink_lv_font_t *xt_font = (xt_eink_lv_font_t *)font;
    if (xt_font->ctx == NULL) {
        return NULL;
    }
    return xt_eink_font_get_bitmap(xt_font->ctx, unicode);
}
