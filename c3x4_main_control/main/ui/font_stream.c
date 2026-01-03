/**
 * @file font_stream.c
 * @brief 流式字体加载器实现（LVGL 9.x 兼容）
 *
 * 通过自定义回调从文件流按需读取字形数据：
 * 1. 只加载字体头信息（几 KB）
 * 2. 按需从文件读取字形位图
 * 3. 使用 LRU 缓存管理字形位图
 */

#include "font_stream.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "FONT_STREAM";

// 字形缓存项
typedef struct {
    uint32_t unicode;          // Unicode 码点
    uint8_t *bitmap;           // 位图数据
    uint16_t bitmap_size;      // 位图大小
    uint16_t stride;           // 行 stride
    bool used;                 // 是否使用
    uint32_t last_access;      // 最后访问时间
} stream_glyph_t;

// 流式字体上下文（存储在 lv_font_t.user_data 中）
typedef struct {
    FILE *fp;                          // 文件指针
    uint32_t file_size;                // 文件大小
    char file_path[256];               // 文件路径

    // 字体头信息
    uint16_t line_height;
    uint16_t base_line;
    uint8_t bpp;
    uint8_t cmap_num;

    // 文件偏移
    uint32_t cmap_offset;
    uint32_t glyph_dsc_offset;
    uint32_t glyph_bitmap_offset;

    // 字形缓存
    stream_glyph_t glyph_cache[GLYPH_CACHE_SIZE];
    uint32_t cache_access_counter;

    // 当前字形的位图（用于 get_glyph_bitmap）
    const uint8_t *current_bitmap;
} stream_font_ctx_t;

// 字形描述符大小
#define GLYPH_DSC_SIZE 24

/**
 * @brief 字体文件头结构
 */
typedef struct __attribute__((packed)) {
    uint32_t version;
    uint32_t magic;
    uint16_t line_height;
    uint16_t base_line;
    uint8_t bpp;
    uint8_t cmap_num;
    uint16_t kern_classes;
    uint8_t bitmap_format;
    uint8_t flags;
    uint32_t cmap_list_offset;
    uint32_t glyph_dsc_offset;
    uint32_t glyph_bitmap_offset;
} lv_font_bin_header_t;

/**
 * @brief cmap 头部结构
 */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t entries;
} lv_font_cmap_header_t;

/**
 * @brief cmap 条目
 */
typedef struct __attribute__((packed)) {
    uint32_t codepoint;
    uint32_t glyph_index;
} lv_font_cmap_entry_t;

/**
 * @brief 字形描述符
 */
typedef struct __attribute__((packed)) {
    uint32_t codepoint;
    uint16_t advance_x;
    uint16_t box_w;
    uint16_t box_h;
    int16_t ofs_x;
    int16_t ofs_y;
    uint32_t bitmap_offset;
} lv_font_glyph_dsc_bin_t;

// 获取 LRU 缓存项
static stream_glyph_t *get_lru_glyph(stream_font_ctx_t *ctx)
{
    stream_glyph_t *lru = &ctx->glyph_cache[0];
    uint32_t min_access = ctx->glyph_cache[0].last_access;

    for (int i = 1; i < GLYPH_CACHE_SIZE; i++) {
        if (!ctx->glyph_cache[i].used ||
            ctx->glyph_cache[i].last_access < min_access) {
            min_access = ctx->glyph_cache[i].last_access;
            lru = &ctx->glyph_cache[i];
        }
    }
    return lru;
}

// 查找缓存项
static stream_glyph_t *find_glyph_cache(stream_font_ctx_t *ctx, uint32_t unicode)
{
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if (ctx->glyph_cache[i].used &&
            ctx->glyph_cache[i].unicode == unicode) {
            ctx->glyph_cache[i].last_access = ++ctx->cache_access_counter;
            return &ctx->glyph_cache[i];
        }
    }
    return NULL;
}

// 清理缓存
static void clear_glyph_cache(stream_font_ctx_t *ctx)
{
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if (ctx->glyph_cache[i].used && ctx->glyph_cache[i].bitmap != NULL) {
            free(ctx->glyph_cache[i].bitmap);
            ctx->glyph_cache[i].bitmap = NULL;
        }
        ctx->glyph_cache[i].used = false;
    }
}

// 从 cmap 查找字形索引
static uint32_t find_glyph_index(stream_font_ctx_t *ctx, uint32_t unicode)
{
    if (ctx->fp == NULL || ctx->cmap_num == 0) {
        return 0xFFFFFFFF;
    }

    for (uint8_t cmap_idx = 0; cmap_idx < ctx->cmap_num; cmap_idx++) {
        uint32_t cmap_offset = ctx->cmap_offset + cmap_idx * sizeof(lv_font_cmap_header_t);
        lv_font_cmap_header_t cmap_header;

        fseek(ctx->fp, cmap_offset, SEEK_SET);
        if (fread(&cmap_header, 1, sizeof(cmap_header), ctx->fp) != sizeof(cmap_header)) {
            continue;
        }

        // 二分查找
        uint32_t left = 0, right = cmap_header.entries;
        uint32_t found_glyph = 0xFFFFFFFF;

        while (left < right) {
            uint32_t mid = (left + right) / 2;
            uint32_t entry_offset = cmap_offset + sizeof(cmap_header) +
                                    mid * sizeof(lv_font_cmap_entry_t);
            lv_font_cmap_entry_t entry;

            fseek(ctx->fp, entry_offset, SEEK_SET);
            if (fread(&entry, 1, sizeof(entry), ctx->fp) != sizeof(entry)) {
                break;
            }

            if (entry.codepoint == unicode) {
                found_glyph = entry.glyph_index;
                break;
            } else if (entry.codepoint < unicode) {
                left = mid + 1;
            } else {
                if (mid == 0) break;
                right = mid;
            }
        }

        if (found_glyph != 0xFFFFFFFF) {
            return found_glyph;
        }
    }

    return 0xFFFFFFFF;
}

// 从文件读取字形描述符
static bool read_glyph_dsc(stream_font_ctx_t *ctx, uint32_t glyph_index,
                           lv_font_glyph_dsc_bin_t *bin_dsc)
{
    if (ctx->fp == NULL || glyph_index == 0xFFFFFFFF) {
        return false;
    }

    uint32_t offset = ctx->glyph_dsc_offset + glyph_index * GLYPH_DSC_SIZE;
    fseek(ctx->fp, offset, SEEK_SET);

    if (fread(bin_dsc, 1, sizeof(lv_font_glyph_dsc_bin_t), ctx->fp) !=
        sizeof(lv_font_glyph_dsc_bin_t)) {
        return false;
    }

    return true;
}

/**
 * @brief 字形描述符回调
 */
static bool font_get_glyph_dsc_cb(const lv_font_t *font,
                                  lv_font_glyph_dsc_t *dsc,
                                  uint32_t unicode,
                                  uint32_t unicode_next)
{
    (void)unicode_next;

    stream_font_ctx_t *ctx = (stream_font_ctx_t *)font->user_data;
    if (ctx == NULL || ctx->fp == NULL) {
        return false;
    }

    // 查找字形索引
    uint32_t glyph_index = find_glyph_index(ctx, unicode);
    if (glyph_index == 0xFFFFFFFF) {
        return false;
    }

    // 读取字形描述符
    lv_font_glyph_dsc_bin_t bin_dsc;
    if (!read_glyph_dsc(ctx, glyph_index, &bin_dsc)) {
        return false;
    }

    // 填充 dsc
    dsc->adv_w = bin_dsc.advance_x;
    dsc->box_w = bin_dsc.box_w;
    dsc->box_h = bin_dsc.box_h;
    dsc->ofs_x = bin_dsc.ofs_x;
    dsc->ofs_y = bin_dsc.ofs_y;

    // 计算 stride
    dsc->stride = (bin_dsc.box_w * ctx->bpp + 7) / 8;

    // 设置格式
    dsc->format = LV_FONT_GLYPH_FORMAT_A1;  // 简化处理
    dsc->is_placeholder = 0;
    dsc->req_raw_bitmap = 0;
    dsc->outline_stroke_width = 0;

    // 查找或加载位图
    stream_glyph_t *glyph = find_glyph_cache(ctx, unicode);
    if (glyph != NULL && glyph->bitmap != NULL) {
        // 使用缓存的位图
        dsc->gid.src = glyph->bitmap;
        ctx->current_bitmap = glyph->bitmap;
        return true;
    }

    // 加载位图
    if (bin_dsc.box_w > 0 && bin_dsc.box_h > 0) {
        uint16_t bitmap_size = dsc->stride * bin_dsc.box_h;
        uint8_t *bitmap = (uint8_t *)malloc(bitmap_size);
        if (bitmap != NULL) {
            uint32_t bitmap_offset = ctx->glyph_bitmap_offset + bin_dsc.bitmap_offset;
            fseek(ctx->fp, bitmap_offset, SEEK_SET);
            if (fread(bitmap, 1, bitmap_size, ctx->fp) == bitmap_size) {
                // 存入缓存
                if (glyph == NULL) {
                    glyph = get_lru_glyph(ctx);
                    if (glyph->bitmap != NULL) {
                        free(glyph->bitmap);
                    }
                    glyph->unicode = unicode;
                    glyph->used = true;
                    glyph->last_access = ++ctx->cache_access_counter;
                }
                glyph->bitmap = bitmap;
                glyph->bitmap_size = bitmap_size;
                glyph->stride = dsc->stride;

                dsc->gid.src = bitmap;
                ctx->current_bitmap = bitmap;
                return true;
            }
            free(bitmap);
        }
    }

    dsc->gid.src = NULL;
    ctx->current_bitmap = NULL;
    return true;
}

/**
 * @brief 字形位图回调
 */
static const void *font_get_bitmap_cb(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    (void)draw_buf;

    if (dsc == NULL) {
        return NULL;
    }

    // 返回存储在 dsc->gid.src 中的位图指针
    return dsc->gid.src;
}

/**
 * @brief 释放字形回调
 */
static void font_release_glyph_cb(const lv_font_t *font, lv_font_glyph_dsc_t *dsc)
{
    (void)font;
    (void)dsc;
    // 暂不实现缓存清理
}

/**
 * @brief 加载字体头信息
 */
static bool load_font_header(stream_font_ctx_t *ctx)
{
    lv_font_bin_header_t header;
    fseek(ctx->fp, 0, SEEK_SET);

    if (fread(&header, 1, sizeof(header), ctx->fp) != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to read font header");
        return false;
    }

    ctx->line_height = header.line_height;
    ctx->base_line = header.base_line;
    ctx->bpp = header.bpp;
    ctx->cmap_num = header.cmap_num;

    ctx->cmap_offset = (header.cmap_list_offset > 0) ?
                       header.cmap_list_offset : sizeof(lv_font_bin_header_t);
    ctx->glyph_dsc_offset = header.glyph_dsc_offset;
    ctx->glyph_bitmap_offset = header.glyph_bitmap_offset;

    ESP_LOGI(TAG, "Font header: h=%d, bpp=%d, cmap=%d, dsc=%lu, bmp=%lu",
             ctx->line_height, ctx->bpp, ctx->cmap_num,
             (unsigned long)ctx->glyph_dsc_offset,
             (unsigned long)ctx->glyph_bitmap_offset);

    return true;
}

stream_font_t *font_stream_open(const char *path)
{
    stream_font_ctx_t *ctx = (stream_font_ctx_t *)malloc(sizeof(stream_font_ctx_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }

    memset(ctx, 0, sizeof(stream_font_ctx_t));
    strncpy(ctx->file_path, path, sizeof(ctx->file_path) - 1);

    ctx->fp = fopen(path, "rb");
    if (ctx->fp == NULL) {
        ESP_LOGE(TAG, "Failed to open: %s", path);
        free(ctx);
        return NULL;
    }

    fseek(ctx->fp, 0, SEEK_END);
    ctx->file_size = ftell(ctx->fp);
    fseek(ctx->fp, 0, SEEK_SET);

    ESP_LOGI(TAG, "Opened: %s (%lu bytes)", path, (unsigned long)ctx->file_size);

    if (!load_font_header(ctx)) {
        fclose(ctx->fp);
        free(ctx);
        return NULL;
    }

    return (stream_font_t *)ctx;
}

void font_stream_close(stream_font_t *font)
{
    if (font == NULL) return;

    stream_font_ctx_t *ctx = (stream_font_ctx_t *)font;
    if (ctx->fp != NULL) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }

    clear_glyph_cache(ctx);
    free(ctx);
}

lv_font_t *font_stream_create(const char *path)
{
    stream_font_ctx_t *ctx = (stream_font_ctx_t *)font_stream_open(path);
    if (ctx == NULL) {
        return NULL;
    }

    lv_font_t *font = (lv_font_t *)malloc(sizeof(lv_font_t));
    if (font == NULL) {
        font_stream_close((stream_font_t *)ctx);
        return NULL;
    }

    font->line_height = ctx->line_height;
    font->base_line = ctx->base_line;
    font->subpx = LV_FONT_SUBPX_NONE;
    font->dsc = NULL;
    font->user_data = ctx;
    font->get_glyph_dsc = font_get_glyph_dsc_cb;
    font->get_glyph_bitmap = font_get_bitmap_cb;
    font->release_glyph = font_release_glyph_cb;

    ESP_LOGI(TAG, "Created stream font: %s", path);

    return font;
}

void font_stream_destroy(lv_font_t *font)
{
    if (font == NULL) return;

    if (font->user_data != NULL) {
        font_stream_close((stream_font_t *)font->user_data);
        font->user_data = NULL;
    }

    free(font);
}

void font_stream_get_info(stream_font_t *font, char *buffer, size_t buffer_size)
{
    if (font == NULL || buffer == NULL) return;

    stream_font_ctx_t *ctx = (stream_font_ctx_t *)font;
    int cached = 0;

    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if (ctx->glyph_cache[i].used && ctx->glyph_cache[i].bitmap != NULL) {
            cached++;
        }
    }

    snprintf(buffer, buffer_size,
             "Stream: %s\n"
             "  Size: %lu bytes\n"
             "  Height: %d, BPP: %d\n"
             "  Cache: %d/%d",
             ctx->file_path,
             (unsigned long)ctx->file_size,
             ctx->line_height,
             ctx->bpp,
             cached, GLYPH_CACHE_SIZE);
}
