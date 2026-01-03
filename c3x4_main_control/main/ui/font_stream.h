/**
 * @file font_stream.h
 * @brief 流式字体加载器 - 从文件流按需读取字形数据
 *
 * 不需要将整个字体文件加载到内存，而是：
 * 1. 加载字体头信息（较小，约几 KB）
 * 2. 按需从文件读取字形位图
 * 3. 实现字形缓存管理
 */

#ifndef FONT_STREAM_H
#define FONT_STREAM_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/

// 缓存的字形数量（根据可用内存调整）
#define GLYPH_CACHE_SIZE 32

// 最大同时打开的字体文件数
#define MAX_OPEN_FONTS 4

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 字形缓存项
 */
typedef struct {
    uint32_t unicode;          // 字形 Unicode 码点
    uint8_t *bitmap;           // 字形位图缓存（NULL 表示未缓存）
    uint16_t bitmap_size;      // 位图大小
    uint16_t bitmap_offset;    // 位图在文件中的偏移
    lv_font_glyph_dsc_t dsc;   // 字形描述符
    bool used;                 // 是否被使用
    uint32_t last_access;      // 最后访问时间（用于 LRU）
} glyph_cache_item_t;

/**
 * @brief 流式字体上下文
 */
typedef struct {
    char file_path[256];               // 字体文件路径
    FILE *fp;                          // 文件指针
    uint32_t file_size;                // 文件大小

    // 字体头信息（加载到内存）
    uint16_t line_height;              // 行高
    uint16_t base_line;                // 基线
    uint8_t bpp;                       // 每像素位数
    uint8_t cmap_num;                  // cmap 数量
    uint16_t kern_classes;             // kern class 数量

    // 文件中各表的偏移量
    uint32_t cmap_offset;              // cmap 表偏移
    uint32_t glyph_dsc_offset;         // 字形描述表偏移
    uint32_t glyph_bitmap_offset;      // 字形位图表偏移

    // 字形缓存
    glyph_cache_item_t glyph_cache[GLYPH_CACHE_SIZE];
    uint32_t cache_access_counter;     // 用于 LRU 排序
} stream_font_context_t;

/**
 * @brief 流式字体句柄
 */
typedef struct {
    stream_font_context_t ctx;
    int ref_count;
} stream_font_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 打开流式字体文件
 * @param path 字体文件路径
 * @return 字体句柄，失败返回 NULL
 */
stream_font_t *font_stream_open(const char *path);

/**
 * @brief 关闭流式字体
 * @param font 字体句柄
 */
void font_stream_close(stream_font_t *font);

/**
 * @brief 获取字形描述符（从文件读取）
 * @param font 字体句柄
 * @param unicode Unicode 码点
 * @param font_height 字体高度
 * @return 字形描述符指针，失败返回 NULL
 */
const lv_font_glyph_dsc_t *font_stream_get_glyph_dsc(stream_font_t *font,
                                                      uint32_t unicode,
                                                      uint32_t font_height);

/**
 * @brief 获取字形位图（从文件读取，可能返回缓存）
 * @param font 字体句柄
 * @param unicode Unicode 码点
 * @return 位图数据指针
 */
const uint8_t *font_stream_get_bitmap(stream_font_t *font, uint32_t unicode);

/**
 * @brief 创建 LVGL 字体对象（使用流式加载）
 * @param path 字体文件路径
 * @return LVGL 字体指针，失败返回 NULL
 */
lv_font_t *font_stream_create(const char *path);

/**
 * @brief 销毁流式字体
 * @param font LVGL 字体指针（font_stream_create 创建的）
 */
void font_stream_destroy(lv_font_t *font);

/**
 * @brief 获取流式字体状态信息
 * @param font 字体句柄
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 */
void font_stream_get_info(stream_font_t *font, char *buffer, size_t buffer_size);

#endif // FONT_STREAM_H
