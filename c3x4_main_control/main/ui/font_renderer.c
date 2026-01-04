/**
 * @file font_renderer.c
 * @brief 字体渲染系统实现
 *
 * 参考：diy-esp32-epub-reader 项目的字体实现
 */

#include "font_renderer.h"
#include "Fonts/fonts.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "FONT_RENDERER";

// 当前字体状态
static int s_current_font_size = DEFAULT_FONT_SIZE;
static bool s_initialized = false;

// 字体缓存（支持多种字体大小）
#define MAX_CACHED_FONTS 6
static font_info_t s_font_cache[MAX_CACHED_FONTS] = {0};

// 字体文件格式（.bin）
// 格式：
//   [0-3]  魔数: 'F' 'O' 'N' 'T'
//   [4-5]  字体大小 (uint16_t)
//   [6-7]  字符宽度 (uint16_t)
//   [8-9]  字符高度 (uint16_t)
//   [10-11] 字符数量 (uint16_t)
//   [12...] 字符位图数据

#define FONT_MAGIC 0x544E4F46  // "FONT"

typedef struct {
    uint32_t magic;
    uint16_t font_size;
    uint16_t char_width;
    uint16_t char_height;
    uint16_t char_count;
} __attribute__((packed)) font_file_header_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static const sFONT* get_ascii_font(int size);
static font_info_t* get_or_create_font_info(int font_size);
static int get_char_index_in_font(uint32_t ch, const uint8_t *data, int char_count);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static const sFONT* get_ascii_font(int size)
{
    switch (size) {
        case FONT_SIZE_8:  return &Font8;
        case FONT_SIZE_12: return &Font12;
        case FONT_SIZE_16: return &Font16;
        case FONT_SIZE_20: return &Font20;
        case FONT_SIZE_24: return &Font24;
        default:
            if (size < 12) return &Font8;
            if (size < 16) return &Font12;
            if (size < 20) return &Font16;
            if (size < 24) return &Font20;
            return &Font24;
    }
}

static font_info_t* get_or_create_font_info(int font_size)
{
    // 查找已存在的字体信息
    for (int i = 0; i < MAX_CACHED_FONTS; i++) {
        if (s_font_cache[i].size == font_size && s_font_cache[i].is_loaded) {
            return &s_font_cache[i];
        }
    }

    // 创建新的字体信息
    for (int i = 0; i < MAX_CACHED_FONTS; i++) {
        if (!s_font_cache[i].is_loaded) {
            s_font_cache[i].size = font_size;
            s_font_cache[i].is_loaded = true;
            s_font_cache[i].width = font_size;  // 假设等宽
            s_font_cache[i].height = font_size;
            return &s_font_cache[i];
        }
    }

    return NULL;
}

static int get_char_index_in_font(uint32_t ch, const uint8_t *data, int char_count)
{
    // TODO: 实现字符索引查找
    // 对于简单的字体文件，可以假设字符按顺序存储
    // 对于 GB2312 字符集，需要根据区位码计算索引
    return -1;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool font_renderer_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Font renderer already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing font renderer...");

    // 清零字体缓存
    memset(s_font_cache, 0, sizeof(s_font_cache));

    // 初始化默认字体（ASCII）
    const sFONT *default_font = get_ascii_font(DEFAULT_FONT_SIZE);
    for (int i = 0; i < MAX_CACHED_FONTS; i++) {
        s_font_cache[i].is_loaded = false;
    }

    // 设置当前字体
    s_current_font_size = DEFAULT_FONT_SIZE;

    s_initialized = true;
    ESP_LOGI(TAG, "Font renderer initialized");
    return true;
}

void font_renderer_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    // 清理字体缓存
    memset(s_font_cache, 0, sizeof(s_font_cache));

    s_initialized = false;
    ESP_LOGI(TAG, "Font renderer deinitialized");
}

bool font_renderer_set_size(int font_size)
{
    const sFONT *font = get_ascii_font(font_size);
    if (font == NULL) {
        ESP_LOGE(TAG, "Invalid font size: %d", font_size);
        return false;
    }

    s_current_font_size = font_size;
    return true;
}

int font_renderer_get_size(void)
{
    return s_current_font_size;
}

bool font_renderer_load_chinese_font(const char *font_path, int font_size)
{
    if (font_path == NULL) {
        ESP_LOGE(TAG, "Font path is NULL");
        return false;
    }

    ESP_LOGI(TAG, "Loading Chinese font: %s (size=%d)", font_path, font_size);

    FILE *fp = fopen(font_path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open font file: %s", font_path);
        return false;
    }

    // 读取文件头
    font_file_header_t header;
    size_t read_size = fread(&header, 1, sizeof(header), fp);
    if (read_size != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to read font header");
        fclose(fp);
        return false;
    }

    // 验证魔数
    if (header.magic != FONT_MAGIC) {
        ESP_LOGE(TAG, "Invalid font magic: 0x%08X", header.magic);
        fclose(fp);
        return false;
    }

    // 验证字体大小
    if (header.font_size != font_size) {
        ESP_LOGW(TAG, "Font size mismatch: file=%d, requested=%d",
                 header.font_size, font_size);
    }

    fclose(fp);

    // 更新字体信息
    font_info_t *info = get_or_create_font_info(font_size);
    if (info != NULL) {
        info->width = header.char_width;
        info->height = header.char_height;
        strncpy(info->font_path, font_path, MAX_FONT_PATH - 1);
        info->font_path[MAX_FONT_PATH - 1] = '\0';
        ESP_LOGI(TAG, "Chinese font loaded: w=%d, h=%d, count=%d",
                 header.char_width, header.char_height, header.char_count);
        return true;
    }

    ESP_LOGE(TAG, "Font cache full");
    return false;
}

bool font_renderer_is_loaded(int font_size)
{
    for (int i = 0; i < MAX_CACHED_FONTS; i++) {
        if (s_font_cache[i].size == font_size && s_font_cache[i].is_loaded) {
            return true;
        }
    }
    return false;
}

const font_info_t* font_renderer_get_info(int font_size)
{
    for (int i = 0; i < MAX_CACHED_FONTS; i++) {
        if (s_font_cache[i].size == font_size && s_font_cache[i].is_loaded) {
            return &s_font_cache[i];
        }
    }

    // 返回默认字体信息
    const sFONT *font = get_ascii_font(font_size);
    static font_info_t default_info;
    default_info.size = font_size;
    default_info.width = font->Width;
    default_info.height = font->Height;
    default_info.is_loaded = true;
    return &default_info;
}

bool font_renderer_get_metrics(const char *text, int font_size, font_metrics_t *metrics)
{
    if (text == NULL || metrics == NULL) {
        return false;
    }

    const font_info_t *info = font_renderer_get_info(font_size);
    if (info == NULL) {
        return false;
    }

    int width = 0;
    int max_height = info->height;

    // 遍历文本计算宽度
    const char *p = text;
    while (*p != '\0') {
        uint32_t ch;
        int offset = font_renderer_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        if (font_renderer_is_chinese(ch)) {
            // 中文字符使用等宽
            width += info->width;
        } else {
            // ASCII 字符使用 GUI_Paint 字体宽度
            const sFONT *font = get_ascii_font(font_size);
            width += font->Width;
        }

        p += offset;
    }

    metrics->width = width;
    metrics->height = max_height;
    metrics->baseline = 0;  // TODO: 实现基线计算

    return true;
}

bool font_renderer_render_char(uint32_t ch, int font_size,
                                uint8_t *output, size_t output_size,
                                int *width, int *height)
{
    if (output == NULL || width == NULL || height == NULL) {
        return false;
    }

    const font_info_t *info = font_renderer_get_info(font_size);
    if (info == NULL) {
        return false;
    }

    // 对于 ASCII 字符，使用 GUI_Paint 内置字体
    if (!font_renderer_is_chinese(ch)) {
        const sFONT *font = get_ascii_font(font_size);
        if (font == NULL) {
            return false;
        }

        // 从字体的 table 数组获取位图
        if (ch >= 32 && ch <= 126) {
            int char_index = ch - 32;
            const uint8_t *bitmap = &font->table[char_index * font->Height * (font->Width / 8)];

            // 计算输出大小
            size_t bitmap_size = font->Height * ((font->Width + 7) / 8);
            if (output_size < bitmap_size) {
                return false;
            }

            memcpy(output, bitmap, bitmap_size);
            *width = font->Width;
            *height = font->Height;
            return true;
        }
    }

    // 对于中文字符，从文件加载
    if (info->font_path[0] != '\0') {
        return font_renderer_render_char_from_file(ch, info->font_path,
                                                     output, output_size,
                                                     width, height);
    }

    // 未找到字符
    return false;
}

bool font_renderer_render_text(const char *text, int font_size,
                                uint8_t *output, size_t output_size,
                                int max_width, font_metrics_t *metrics)
{
    if (text == NULL || output == NULL) {
        return false;
    }

    // TODO: 实现文本渲染
    // 1. 计算每个字符的宽度
    // 2. 将字符位图拼接在一起
    // 3. 处理换行（如果指定了 max_width）

    return false;
}

bool font_renderer_render_char_from_file(uint32_t ch, const char *font_path,
                                          uint8_t *output, size_t output_size,
                                          int *width, int *height)
{
    if (font_path == NULL || output == NULL || width == NULL || height == NULL) {
        return false;
    }

    FILE *fp = fopen(font_path, "rb");
    if (fp == NULL) {
        return false;
    }

    // 读取文件头
    font_file_header_t header;
    fread(&header, 1, sizeof(header), fp);

    if (header.magic != FONT_MAGIC) {
        fclose(fp);
        return false;
    }

    // TODO: 实现字符查找和位图读取
    // 1. 根据字符编码计算索引
    // 2. 定位到位图数据
    // 3. 读取位图到输出缓冲区

    fclose(fp);
    return false;
}

int font_renderer_utf8_to_utf32(const char *utf8, uint32_t *out_utf32)
{
    if (utf8 == NULL || out_utf32 == NULL) {
        return 0;
    }

    uint8_t b0 = (uint8_t)utf8[0];

    if ((b0 & 0x80) == 0) {
        // 1-byte: 0xxxxxxx
        *out_utf32 = b0;
        return 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        // 2-byte: 110xxxxx 10xxxxxx
        *out_utf32 = ((b0 & 0x1F) << 6) | ((uint8_t)utf8[1] & 0x3F);
        return 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
        *out_utf32 = ((b0 & 0x0F) << 12) |
                     ((uint8_t)utf8[1] & 0x3F) << 6 |
                     ((uint8_t)utf8[2] & 0x3F);
        return 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        *out_utf32 = ((b0 & 0x07) << 18) |
                     ((uint8_t)utf8[1] & 0x3F) << 12 |
                     ((uint8_t)utf8[2] & 0x3F) << 6 |
                     ((uint8_t)utf8[3] & 0x3F);
        return 4;
    }

    *out_utf32 = b0;
    return 1;
}

int font_renderer_scan_directory(const char *font_dir)
{
    if (font_dir == NULL) {
        return 0;
    }

    // TODO: 实现目录扫描
    // 1. 打开目录
    // 2. 遍历 .bin 文件
    // 3. 解析文件头获取字体大小
    // 4. 返回字体数量

    return 0;
}
