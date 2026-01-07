/**
 * @file xt_eink_font.h
 * @brief XTEinkFontBinary 字体解析器 - 阅星曈自定义字体格式支持
 *
 * XTEinkFontBinary 格式说明：
 * - 每个字符占用固定大小的位图区域
 * - 位图按行优先存储，每行按字节对齐
 * - 1-bit 色深，适合电子墨水屏
 *
 * 文件命名格式：{字体名} {宽度}×{高度}.bin
 */

#ifndef XT_EINK_FONT_H
#define XT_EINK_FONT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/

// 最大同时打开的字体文件数
#define XT_EINK_FONT_MAX_OPEN 4

// 字形缓存大小
#define XT_EINK_GLYPH_CACHE_SIZE 16

// XTEinkFontBinary 文件头 Magic Number (ASCII "XTF")
#define XT_EINK_MAGIC 0x58454620  // "XTF " (带版本标识)

// 版本号
#define XT_EINK_VERSION 1

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief XTEink 字体文件头
 */
typedef struct {
    uint32_t magic;           // Magic number: 0x58454620 ("XTF ")
    uint8_t version;          // 版本号
    uint8_t width;            // 字体宽度（像素）
    uint8_t height;           // 字体高度（像素）
    uint8_t bpp;              // 每像素位数 (1 = 1-bit)
    uint32_t char_count;      // 字符数量
    uint32_t first_char;      // 第一个字符的 Unicode 码点
    uint32_t last_char;       // 最后一个字符的 Unicode 码点
    uint32_t glyph_size;      // 每个字形占用的字节数
    uint32_t file_size;       // 文件总大小
    uint8_t reserved[8];      // 保留字段
} xt_eink_font_header_t;

/**
 * @brief 字形缓存项
 */
typedef struct {
    uint32_t unicode;              // Unicode 码点
    uint8_t *bitmap;               // 位图缓存
    uint16_t bitmap_size;          // 位图大小
    bool cached;                   // 是否已缓存
    uint32_t last_access;          // 最后访问时间（LRU）
} xt_eink_glyph_cache_t;

/**
 * @brief XTEink 字体上下文
 */
typedef struct {
    char file_path[256];           // 字体文件路径
    FILE *fp;                      // 文件指针（分区模式下为 NULL）
    uint32_t file_size;            // 文件大小

    // 字体信息
    xt_eink_font_header_t header;  // 文件头
    uint16_t width;                // 字体宽度
    uint16_t height;               // 字体高度
    uint16_t glyph_size;           // 每个字形大小（字节）
    uint16_t line_height;          // 行高（等于字体高度）

    // 数据源模式
    bool _use_partition;           // 是否使用 Flash 分区而非文件

    // 字形缓存
    xt_eink_glyph_cache_t cache[XT_EINK_GLYPH_CACHE_SIZE];
    uint32_t cache_access_counter; // LRU 计数器
    uint32_t cache_hit;            // 缓存命中次数
    uint32_t cache_miss;           // 缓存未命中次数
} xt_eink_font_t;

/**
 * @brief 字形描述符（无 LVGL 版本）
 */
typedef struct {
    uint16_t adv_w;                // 字形宽度（像素）
    uint8_t box_w;                 // 字形框宽度
    uint8_t box_h;                 // 字形框高度
    int8_t ofs_x;                  // X 偏移
    int8_t ofs_y;                  // Y 偏移
    uint8_t bpp;                   // 每像素位数
} xt_eink_font_glyph_dsc_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 打开 XTEink 字体文件
 * @param path 字体文件路径
 * @return 字体上下文指针，失败返回 NULL
 */
xt_eink_font_t *xt_eink_font_open(const char *path);

/**
 * @brief 从 Flash 分区打开菜单字体
 * 
 * 此函数专门用于从 font_data 分区加载菜单字体，
 * 避免依赖 SD 卡。当分区不可用时返回 NULL。
 *
 * @return 字体上下文指针（用于菜单），失败返回 NULL
 */
xt_eink_font_t *xt_eink_font_open_partition(void);

/**
 * @brief 关闭 XTEink 字体
 * @param font 字体上下文指针
 */
void xt_eink_font_close(xt_eink_font_t *font);

/**
 * @brief 获取字形描述符（无 LVGL 版本）
 * @param font 字体上下文
 * @param unicode Unicode 码点
 * @param font_height 字体高度（保留参数，允许传 0）
 * @return 字形描述符指针，失败返回 NULL
 */
const xt_eink_font_glyph_dsc_t *xt_eink_font_get_glyph_dsc(xt_eink_font_t *font,
                                                           uint32_t unicode,
                                                           uint32_t font_height);

/**
 * @brief 获取字形位图
 * @param font 字体上下文
 * @param unicode Unicode 码点
 * @return 位图数据指针，失败返回 NULL
 */
const uint8_t *xt_eink_font_get_bitmap(xt_eink_font_t *font, uint32_t unicode);


/**
 * @brief 获取字体信息字符串
 * @param font 字体上下文
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 */
void xt_eink_font_get_info(xt_eink_font_t *font, char *buffer, size_t buffer_size);

/**
 * @brief 检查文件是否为 XTEink 字体格式
 * @param path 文件路径
 * @return true 是 XTEink 字体，false 不是
 */
bool xt_eink_font_is_valid(const char *path);

/**
 * @brief 获取字形缓存统计信息
 * @param font 字体上下文
 * @param hit 输出缓存命中次数
 * @param miss 输出缓存未命中次数
 */
void xt_eink_font_get_cache_stats(xt_eink_font_t *font, uint32_t *hit, uint32_t *miss);

/**
 * @brief 清除字形缓存
 * @param font 字体上下文
 */
void xt_eink_font_clear_cache(xt_eink_font_t *font);

#endif // XT_EINK_FONT_H
