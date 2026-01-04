/**
 * @file chinese_font_impl.c
 * @brief 中文字体实现 - 流式读取 binfont 格式
 *
 * 从 SD 卡直接读取字体文件，不预加载到内存
 * 支持 LVGL binfont v4 格式
 */

#include "chinese_font_impl.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "CHINESE_FONT";

// 调试日志
#define DEBUG_FONT_FORMAT 1

// 字形描述符大小 (24 字节)
#define GLYPH_DSC_SIZE 24

// 字体文件句柄和基本信息
static struct {
    FILE *fp;                    // 字体文件指针
    uint32_t file_size;          // 文件大小
    uint16_t font_height;        // 字体高度
    uint16_t font_width;         // 字体宽度
    uint32_t glyph_count;        // 字形数量
    uint32_t glyph_dsc_offset;   // 字形描述符偏移
    uint32_t bitmap_offset;      // 位图数据偏移
    bool loaded;                 // 是否已打开
} s_font_state = {0};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool read_font_header(const char *font_path);
static bool read_glyph_descriptor(FILE *fp, uint32_t offset, uint32_t *ch,
                                   uint8_t *width, uint8_t *height,
                                   uint32_t *bitmap_offset);
static int find_glyph_offset(FILE *fp, uint32_t glyph_count, uint32_t dsc_offset,
                             uint32_t ch, uint32_t *out_offset);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 读取字体头部信息
 */
static bool read_font_header(const char *font_path)
{
#if DEBUG_FONT_FORMAT
    ESP_LOGI(TAG, "Opening font: %s", font_path);
#endif

    s_font_state.fp = fopen(font_path, "rb");
    if (s_font_state.fp == NULL) {
        ESP_LOGE(TAG, "Failed to open font file: %s", font_path);
        return false;
    }

    // 获取文件大小
    fseek(s_font_state.fp, 0, SEEK_END);
    s_font_state.file_size = ftell(s_font_state.fp);
    fseek(s_font_state.fp, 0, SEEK_SET);

#if DEBUG_FONT_FORMAT
    ESP_LOGI(TAG, "Font file size: %u bytes", s_font_state.file_size);
#endif

    if (s_font_state.file_size < 64) {
        ESP_LOGE(TAG, "Font file too small");
        fclose(s_font_state.fp);
        s_font_state.fp = NULL;
        return false;
    }

    // 读取头部 32 字节
    uint8_t header[32];
    if (fread(header, 1, 32, s_font_state.fp) != 32) {
        ESP_LOGE(TAG, "Failed to read header");
        fclose(s_font_state.fp);
        s_font_state.fp = NULL;
        return false;
    }

    // 读取魔数 (小端)
    uint32_t magic = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
    if (magic != 0x30) {
        ESP_LOGE(TAG, "Invalid magic: 0x%08X", magic);
        fclose(s_font_state.fp);
        s_font_state.fp = NULL;
        return false;
    }

#if DEBUG_FONT_FORMAT
    ESP_LOGI(TAG, "=== Font Header Debug ===");
    ESP_LOGI(TAG, "Magic: 0x%08X", magic);
    for (int i = 0; i < 8; i++) {
        uint32_t val = header[i*4] | (header[i*4+1] << 8) | (header[i*4+2] << 16) | (header[i*4+3] << 24);
        ESP_LOGI(TAG, "  Offset %d (0x%02X): 0x%08X (%d)", i * 4, i * 4, val, val);
    }
    // 打印原始字节
    ESP_LOGI(TAG, "Header bytes:");
    for (int i = 0; i < 32; i++) {
        ESP_LOGI(TAG, "  [%d] 0x%02X", i, header[i]);
    }
    ESP_LOGI(TAG, "============================");
#endif

    // 解析 binfont v4 格式
    // 偏移 0-3:   magic (0x30)
    // 偏移 4-7:   "head"
    // 偏移 8-11:  version
    // 偏移 12-13: font_size (16位)
    // 偏移 14-15: line_height (16位)
    // 偏移 28-31: glyph_dsc_offset (大端序 4 字节, 但格式不统一)

    uint16_t font_size = header[12];
    uint16_t line_height = header[14];
    uint32_t glyph_dsc_offset;
    uint32_t glyph_cnt;

    // 尝试从偏移 28-31 读取
    uint32_t offset_28_31 = ((uint32_t)header[28]) |
                            ((uint32_t)header[29] << 8) |
                            ((uint32_t)header[30] << 16) |
                            ((uint32_t)header[31] << 24);

#if DEBUG_FONT_FORMAT
    ESP_LOGI(TAG, "Offset 28-31 value: 0x%08X", offset_28_31);
#endif

    // 策略：搜索第一个有效的 Unicode CJK 字符来确定 glyph descriptor 偏移
    // CJK 统一汉字范围: 0x4E00-0x9FFF
    // 字形描述符结构: character_code(4) + advance_x(4) + offset_x(2) + offset_y(2) + bpp(1) + w(1) + h(1) + bitmap_offset(4) = 24 字节

    // 已知的工作偏移值（按优先顺序）
    const uint32_t known_offsets[] = {128, 84, 66, 0};  // 128 适用于 chinese_font_20.bin
    bool found = false;

    // 首先尝试已知偏移
    for (int i = 0; known_offsets[i] != 0; i++) {
        uint32_t test_offset = known_offsets[i];
        if (test_offset + 24 > s_font_state.file_size) continue;

        uint8_t test_bytes[4];
        fseek(s_font_state.fp, test_offset, SEEK_SET);
        if (fread(test_bytes, 1, 4, s_font_state.fp) != 4) continue;

        uint32_t ch = test_bytes[0] | (test_bytes[1] << 8) |
                      (test_bytes[2] << 16) | (test_bytes[3] << 24);

        // 检查是否在有效的 CJK 范围内
#if DEBUG_FONT_FORMAT
        ESP_LOGI(TAG, "  test offset=%u, bytes=%02X %02X %02X %02X, ch=0x%08X",
                 test_offset, test_bytes[0], test_bytes[1], test_bytes[2], test_bytes[3], ch);
#endif
        if (ch >= 0x4E00 && ch <= 0x9FFF) {
            // 验证下一个描述符也是有效的 CJK 字符
            uint8_t next_bytes[4];
            fseek(s_font_state.fp, test_offset + 24, SEEK_SET);
            if (fread(next_bytes, 1, 4, s_font_state.fp) == 4) {
                uint32_t next_ch = next_bytes[0] | (next_bytes[1] << 8) |
                                   (next_bytes[2] << 16) | (next_bytes[3] << 24);
                if (next_ch >= 0x4E00 && next_ch <= 0x9FFF) {
                    glyph_dsc_offset = test_offset;
                    found = true;
#if DEBUG_FONT_FORMAT
                    ESP_LOGI(TAG, "Found glyph offset at %u (0x%X), first char=0x%04X, next=0x%04X",
                             test_offset, test_offset, ch, next_ch);
#endif
                    break;
                }
            }
        }
    }

    // 如果已知偏移都不对，搜索整个文件找第一个有效的 CJK 字符
    // 必须验证两个连续的描述符都是有效的 CJK 字符
    if (!found) {
#if DEBUG_FONT_FORMAT
        ESP_LOGI(TAG, "Searching for glyph offset...");
#endif
        // 搜索范围: 32 到文件大小的一半，步进 8 字节
        for (uint32_t pos = 32; pos < s_font_state.file_size / 2; pos += 8) {
            if (pos + 24 > s_font_state.file_size) break;

            uint8_t bytes[4];
            fseek(s_font_state.fp, pos, SEEK_SET);
            if (fread(bytes, 1, 4, s_font_state.fp) != 4) continue;

            uint32_t ch = bytes[0] | (bytes[1] << 8) |
                          (bytes[2] << 16) | (bytes[3] << 24);

            if (ch >= 0x4E00 && ch <= 0x9FFF) {
                // 验证下一个描述符也是 CJK 字符
                uint8_t next_bytes[4];
                fseek(s_font_state.fp, pos + 24, SEEK_SET);
                if (fread(next_bytes, 1, 4, s_font_state.fp) == 4) {
                    uint32_t next_ch = next_bytes[0] | (next_bytes[1] << 8) |
                                       (next_bytes[2] << 16) | (next_bytes[3] << 24);
                    if (next_ch >= 0x4E00 && next_ch <= 0x9FFF) {
                        glyph_dsc_offset = pos;
                        found = true;
#if DEBUG_FONT_FORMAT
                        ESP_LOGI(TAG, "Found glyph offset at %u, char=0x%04X, next=0x%04X",
                                 pos, ch, next_ch);
#endif
                        break;
                    }
                }
            }
        }
    }

    if (!found) {
        // 回退到默认值
        glyph_dsc_offset = 128;
    }

    // 从文件大小估算字形数量
    glyph_cnt = (s_font_state.file_size - glyph_dsc_offset) / GLYPH_DSC_SIZE;

    s_font_state.glyph_count = glyph_cnt;
    s_font_state.glyph_dsc_offset = glyph_dsc_offset;
    s_font_state.font_height = line_height > 0 ? line_height : 16;
    s_font_state.font_width = font_size > 0 ? font_size : 14;
    s_font_state.bitmap_offset = glyph_dsc_offset + glyph_cnt * GLYPH_DSC_SIZE;

#if DEBUG_FONT_FORMAT
    ESP_LOGI(TAG, "Parsed: font_size=%u, line_height=%u", font_size, line_height);
    ESP_LOGI(TAG, "Glyphs: %u, dsc_offset=%u, bitmap_offset=%u",
             glyph_cnt, glyph_dsc_offset, s_font_state.bitmap_offset);
#endif

    return true;
}

/**
 * @brief 从文件读取字形描述符
 */
static bool read_glyph_descriptor(FILE *fp, uint32_t offset, uint32_t *ch,
                                   uint8_t *width, uint8_t *height,
                                   uint32_t *bitmap_offset)
{
    uint8_t desc[GLYPH_DSC_SIZE];

    if (fseek(fp, offset, SEEK_SET) != 0) {
        return false;
    }

    if (fread(desc, 1, GLYPH_DSC_SIZE, fp) != GLYPH_DSC_SIZE) {
        return false;
    }

    // 解析描述符
    *ch = desc[0] | (desc[1] << 8) | (desc[2] << 16) | (desc[3] << 24);
    // advance_x: desc[4-7] (忽略)
    // offset_x: desc[8-9] (忽略)
    // offset_y: desc[10-11] (忽略)
    // bpp: desc[12]
    *width = desc[13];
    *height = desc[14];
    *bitmap_offset = desc[15] | (desc[16] << 8) | (desc[17] << 16) | (desc[18] << 24);

    return true;
}

/**
 * @brief 查找字符在文件中的偏移
 * @return true 找到，false 未找到
 */
static int find_glyph_offset(FILE *fp, uint32_t glyph_count, uint32_t dsc_offset,
                             uint32_t ch, uint32_t *out_offset)
{
    uint32_t desc_offset;
    uint32_t glyph_ch;
    uint8_t width, height;
    uint32_t bitmap_ofs;

    // 二分查找优化（如果字形按 Unicode 排序）
    // 这里先使用线性查找
    for (uint32_t i = 0; i < glyph_count; i++) {
        desc_offset = dsc_offset + i * GLYPH_DSC_SIZE;

        if (!read_glyph_descriptor(fp, desc_offset, &glyph_ch, &width, &height, &bitmap_ofs)) {
            break;
        }

#if DEBUG_FONT_FORMAT
        if (i < 5) {
            ESP_LOGI(TAG, "  [%u] offset=%u, char=0x%08X, w=%d, h=%d",
                     i, desc_offset, glyph_ch, width, height);
        }
#endif

        if (glyph_ch == ch) {
            *out_offset = desc_offset;
            return true;
        }

        // 如果当前字符已经超过目标字符，可以提前退出（假设已排序）
        if (glyph_ch > ch && ch < 0x10000) {
            break;
        }
    }

    return false;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool chinese_font_init(void)
{
    if (s_font_state.loaded) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing Chinese font...");

    // 尝试打开字体文件
    const char *font_paths[] = {
        "/sdcard/fonts/chinese_font_20.bin",
        "/sdcard/fonts/chinese_font.bin",
        "/sdcard/fonts/GenJyuuGothic-Monospace-Light-14-Full.bin",
        NULL
    };

    for (int i = 0; font_paths[i] != NULL; i++) {
        if (read_font_header(font_paths[i])) {
            s_font_state.loaded = true;
            ESP_LOGI(TAG, "Font loaded: %s", font_paths[i]);
            return true;
        }
    }

    ESP_LOGW(TAG, "No font file found");
    s_font_state.font_height = 16;
    s_font_state.font_width = 14;
    s_font_state.loaded = true;  // 标记为已加载（演示模式）

    return true;
}

bool chinese_font_has_char(uint32_t ch)
{
    // 支持中日韩统一表意文字
    if (ch >= 0x4E00 && ch <= 0x9FFF) {
        return true;
    }
    // 支持中文标点
    if ((ch >= 0x3000 && ch <= 0x303F) || (ch >= 0xFF00 && ch <= 0xFFEF)) {
        return true;
    }
    // 如果字体已打开，检查是否真的存在
    if (s_font_state.loaded && s_font_state.fp != NULL) {
        uint32_t offset;
        if (find_glyph_offset(s_font_state.fp, s_font_state.glyph_count,
                              s_font_state.glyph_dsc_offset, ch, &offset)) {
            return true;
        }
    }
    return false;
}

bool chinese_font_get_glyph(uint32_t ch, chinese_glyph_t *glyph)
{
    if (glyph == NULL || s_font_state.fp == NULL) {
        return false;
    }

    // 查找字形描述符
    uint32_t desc_offset;
    if (!find_glyph_offset(s_font_state.fp, s_font_state.glyph_count,
                           s_font_state.glyph_dsc_offset, ch, &desc_offset)) {
        return false;
    }

    // 读取描述符
    uint32_t glyph_ch;
    uint8_t width, height;
    uint32_t bitmap_offset;
    if (!read_glyph_descriptor(s_font_state.fp, desc_offset, &glyph_ch,
                               &width, &height, &bitmap_offset)) {
        return false;
    }

    // 读取位图数据
    uint32_t file_bitmap_offset = s_font_state.bitmap_offset + bitmap_offset;
    uint32_t bitmap_size = ((width + 7) / 8) * height;

    if (fseek(s_font_state.fp, file_bitmap_offset, SEEK_SET) != 0) {
        return false;
    }

    static uint8_t bitmap[128];
    if (fread(bitmap, 1, bitmap_size, s_font_state.fp) != bitmap_size) {
        return false;
    }

    glyph->bitmap = bitmap;
    glyph->width = width;
    glyph->height = height;
    glyph->bpp = 1;
    glyph->glyph_index = ch;

    return true;
}

int chinese_font_render_char(int x, int y, uint32_t ch, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (framebuffer == NULL || s_font_state.fp == NULL) {
        return s_font_state.font_width;
    }

    // 获取字形
    chinese_glyph_t glyph;
    if (!chinese_font_get_glyph(ch, &glyph)) {
        return s_font_state.font_width;  // 默认宽度
    }

    // 渲染 1bpp 位图
    int bytes_per_row = (glyph.width + 7) / 8;

    for (int row = 0; row < glyph.height; row++) {
        for (int col = 0; col < glyph.width; col++) {
            int fb_x = x + col;
            int fb_y = y + row;

            if (fb_x < 0 || fb_x >= fb_width || fb_y < 0 || fb_y >= fb_height) {
                continue;
            }

            int byte_idx = row * bytes_per_row + col / 8;
            int bit_idx = 7 - (col % 8);
            bool pixel_set = (glyph.bitmap[byte_idx] >> bit_idx) & 1;

            int fb_byte_idx = fb_y * ((fb_width + 7) / 8) + fb_x / 8;
            int fb_bit_idx = 7 - (fb_x % 8);

            if (pixel_set) {
                framebuffer[fb_byte_idx] |= (color << fb_bit_idx);
            } else {
                framebuffer[fb_byte_idx] &= ~(1 << fb_bit_idx);
            }
        }
    }

    return glyph.width;
}

int chinese_font_render_text(int x, int y, const char *text, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (text == NULL || framebuffer == NULL) {
        return 0;
    }

    int current_x = x;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = chinese_font_utf8_to_utf32(p, &ch);

        if (offset <= 0) {
            break;
        }

        int char_width;
        if (ch >= 0x80 || chinese_font_has_char(ch)) {
            char_width = chinese_font_render_char(current_x, y, ch, color,
                                                   framebuffer, fb_width, fb_height);
        } else {
            char_width = 8;  // ASCII
        }

        current_x += char_width;
        p += offset;
    }

    return current_x - x;
}

int chinese_font_get_text_width(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    int width = 0;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = chinese_font_utf8_to_utf32(p, &ch);

        if (offset <= 0) {
            break;
        }

        chinese_glyph_t glyph;
        if (chinese_font_get_glyph(ch, &glyph)) {
            width += glyph.width;
        } else if (ch < 0x80) {
            width += 8;  // ASCII
        } else {
            width += s_font_state.font_width;
        }

        p += offset;
    }

    return width;
}

int chinese_font_get_height(void)
{
    return s_font_state.font_height > 0 ? s_font_state.font_height : 16;
}

int chinese_font_get_width(void)
{
    return s_font_state.font_width > 0 ? s_font_state.font_width : 14;
}

int chinese_font_utf8_to_utf32(const char *utf8, uint32_t *out_utf32)
{
    if (utf8 == NULL || out_utf32 == NULL) {
        return 0;
    }

    uint8_t b0 = (uint8_t)utf8[0];

    if ((b0 & 0x80) == 0) {
        *out_utf32 = b0;
        return 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        *out_utf32 = ((b0 & 0x1F) << 6) | ((uint8_t)utf8[1] & 0x3F);
        return 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        *out_utf32 = ((b0 & 0x0F) << 12) |
                     ((uint8_t)utf8[1] & 0x3F) << 6 |
                     ((uint8_t)utf8[2] & 0x3F);
        return 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        *out_utf32 = ((b0 & 0x07) << 18) |
                     ((uint8_t)utf8[1] & 0x3F) << 12 |
                     ((uint8_t)utf8[2] & 0x3F) << 6 |
                     ((uint8_t)utf8[3] & 0x3F);
        return 4;
    }

    *out_utf32 = b0;
    return 1;
}
