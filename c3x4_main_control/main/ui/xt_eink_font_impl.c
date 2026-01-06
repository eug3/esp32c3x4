/**
 * @file xt_eink_font_impl.c
 * @brief XTEink 字体渲染实现
 */

#include "xt_eink_font_impl.h"
#include "xt_eink_font.h"
#include "font_cache.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "XT_FONT_IMPL";

// 字体上下文
static xt_eink_font_t *s_font = NULL;

// 已加载字体路径（用于初始化缓存）
static char s_loaded_font_path[128] = {0};

// 字形缓冲区
static uint8_t s_glyph_buffer[256];  // 最大字形大小

static uint32_t count_bits_set(const uint8_t *buf, size_t len)
{
    if (buf == NULL) {
        return 0;
    }
    uint32_t total = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        // popcount for byte
        b = (uint8_t)(b - ((b >> 1) & 0x55));
        b = (uint8_t)((b & 0x33) + ((b >> 2) & 0x33));
        total += (uint32_t)((b + (b >> 4)) & 0x0F);
    }
    return total;
}

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
    const char *p = s + (sl - su);
    for (size_t i = 0; i < su; i++) {
        char a = (char)tolower((unsigned char)p[i]);
        char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool contains_ignore_case(const char *s, const char *needle)
{
    if (s == NULL || needle == NULL) {
        return false;
    }
    size_t nl = strlen(needle);
    if (nl == 0) {
        return true;
    }

    for (const char *p = s; *p != '\0'; p++) {
        size_t i = 0;
        while (i < nl) {
            char a = p[i];
            if (a == '\0') {
                return false;
            }
            a = (char)tolower((unsigned char)a);
            char b = (char)tolower((unsigned char)needle[i]);
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == nl) {
            return true;
        }
    }
    return false;
}

static bool try_load_font_by_scanning_dir(const char *dir_path)
{
    if (dir_path == NULL) {
        return false;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        int err = errno;
        ESP_LOGW(TAG, "Font dir not accessible: %s (errno=%d: %s)", dir_path, err, strerror(err));
        return false;
    }

    // 目标：19x25 => width_byte=3, glyph_size=3*25=75 bytes => charByte=75
    const uint32_t desired_char_byte = 75;

    char best_path[192] = {0};
    bool best_is_msyh = false;
    uint32_t best_char_byte = 0;

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (!ends_with_ignore_case(name, ".bin")) {
            continue;
        }

        char fullpath[192];
        int n = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, name);
        if (n <= 0 || (size_t)n >= sizeof(fullpath)) {
            continue;
        }

        struct stat st;
        if (stat(fullpath, &st) != 0) {
            continue;
        }
        if (st.st_size <= 0) {
            continue;
        }
        uint32_t sz = (uint32_t)st.st_size;
        if ((sz % 0x10000u) != 0) {
            continue;
        }
        uint32_t char_byte = sz / 0x10000u;
        bool is_msyh = contains_ignore_case(name, "msyh");

        // 打分策略：优先选择 charByte==75；同为 75 时优先 MSYH。
        // 若目录里没有 75，则退化为选择任意有效字体（仍优先 MSYH）。
        bool take = false;
        if (best_path[0] == '\0') {
            take = true;
        } else if (char_byte == desired_char_byte && best_char_byte != desired_char_byte) {
            take = true;
        } else if (char_byte == best_char_byte) {
            if (is_msyh && !best_is_msyh) {
                take = true;
            }
        } else if (best_char_byte != desired_char_byte) {
            // 都不是 75：更偏好更接近 75
            uint32_t d_new = (char_byte > desired_char_byte) ? (char_byte - desired_char_byte) : (desired_char_byte - char_byte);
            uint32_t d_old = (best_char_byte > desired_char_byte) ? (best_char_byte - desired_char_byte) : (desired_char_byte - best_char_byte);
            if (d_new < d_old) {
                take = true;
            } else if (d_new == d_old && is_msyh && !best_is_msyh) {
                take = true;
            }
        }

        if (take) {
            strncpy(best_path, fullpath, sizeof(best_path) - 1);
            best_path[sizeof(best_path) - 1] = '\0';
            best_is_msyh = is_msyh;
            best_char_byte = char_byte;
        }
    }

    closedir(dir);

    if (best_path[0] == '\0') {
        return false;
    }

    ESP_LOGW(TAG, "Falling back to directory-scan font: %s (charByte=%lu)", best_path, (unsigned long)best_char_byte);
    s_font = xt_eink_font_open(best_path);
    if (s_font == NULL) {
        ESP_LOGE(TAG, "Directory-scan font open failed: %s", best_path);
        return false;
    }
    ESP_LOGI(TAG, "Font loaded successfully (dir scan): %s", best_path);
    return true;
}

/**
 * @brief UTF-8 到 UTF-32 转换
 */
int xt_eink_font_utf8_to_utf32(const char *utf8, uint32_t *out_utf32)
{
    if (utf8 == NULL || out_utf32 == NULL) {
        return 0;
    }

    const unsigned char *p = (const unsigned char *)utf8;

    if (p[0] < 0x80) {
        // 单字节 ASCII
        *out_utf32 = p[0];
        return 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        // 双字节
        if (p[1] == 0) return 0;
        *out_utf32 = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        // 三字节（大多数中文）
        if (p[1] == 0 || p[2] == 0) return 0;
        *out_utf32 = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    } else if ((p[0] & 0xF8) == 0xF0) {
        // 四字节
        if (p[1] == 0 || p[2] == 0 || p[3] == 0) return 0;
        *out_utf32 = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                     ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }

    return 0;
}

bool xt_eink_font_init(void)
{
    if (s_font != NULL) {
        return true;  // 已初始化
    }

    // 尝试加载默认字体（优先使用带像素尺寸的 raw 字体文件名）
    // 注意：若 FATFS 未启用 LFN 或不支持 UTF-8 文件名，包含中文/"×" 的文件名可能 fopen 失败。
#if defined(CONFIG_FATFS_LFN_NONE) && CONFIG_FATFS_LFN_NONE
    ESP_LOGW(TAG, "FATFS LFN is disabled (8.3 only). Long/Unicode filenames will fail. Enable CONFIG_FATFS_LFN_STACK/HEAP.");
#endif
    const char *font_paths[] = {
        "/sdcard/fonts/msyh-14.25pt.19×25.bin",
        "/sdcard/fonts/msyh-14.25pt.19x25.bin",
        "/sdcard/fonts/MSYH-14.25PT.19X25.BIN",

        "/sdcard/fonts/微软雅黑 14.25pt.19×25.bin",
        "/sdcard/字体/微软雅黑 14.25pt.19×25.bin",

        // ASCII 备用名（推荐实际落盘用这个，兼容性最好）
        "/sdcard/fonts/msyh_14_25pt_19x25.bin",
        "/sdcard/fonts/msyh_19x25.bin",
        "/sdcard/fonts/msyh19x25.bin",
        "/sdcard/fonts/MSYH1925.BIN",

        // 兜底（旧默认）
        "/fonts/msyh-14.bin",
        "/sdcard/fonts/msyh-14.bin",
        "/sdcard/字体/msyh-14.bin",
        NULL
    };

    for (int i = 0; font_paths[i] != NULL; i++) {
        ESP_LOGI(TAG, "Trying to load font: %s", font_paths[i]);
        s_font = xt_eink_font_open(font_paths[i]);
        if (s_font != NULL) {
            ESP_LOGI(TAG, "Font loaded successfully: %s", font_paths[i]);

            // 保存字体路径
            strncpy(s_loaded_font_path, font_paths[i], sizeof(s_loaded_font_path) - 1);

            // 初始化智能缓存系统
            if (!font_cache_init(font_paths[i])) {
                ESP_LOGW(TAG, "Font cache init failed, will use direct SD card access");
            } else {
                uint32_t cached_chars = 0;
                font_cache_get_stats(NULL, NULL, &cached_chars);
                ESP_LOGI(TAG, "Font cache ready: %lu common chars in Flash", (unsigned long)cached_chars);
            }

            // 自检：读取几个常见汉字的位图，统计置位数量，帮助判断是否读到了有效数据
            const uint32_t probe_chars[] = { 0x6587u /* 文 */, 0x8BBEu /* 设 */, 0x7F6Eu /* 置 */ };
            for (size_t k = 0; k < sizeof(probe_chars) / sizeof(probe_chars[0]); k++) {
                const uint8_t *bmp = xt_eink_font_get_bitmap(s_font, probe_chars[k]);
                if (bmp == NULL) {
                    ESP_LOGW(TAG, "Probe U+%04lX bitmap=NULL (w=%u h=%u glyph=%u)",
                             (unsigned long)probe_chars[k],
                             (unsigned int)s_font->width, (unsigned int)s_font->height,
                             (unsigned int)s_font->glyph_size);
                    continue;
                }
                size_t sample_len = s_font->glyph_size;
                if (sample_len > 64) {
                    sample_len = 64;
                }
                uint32_t bits = count_bits_set(bmp, sample_len);
                ESP_LOGI(TAG, "Probe U+%04lX bits_set(first %u bytes)=%lu (w=%u h=%u glyph=%u)",
                         (unsigned long)probe_chars[k], (unsigned int)sample_len, (unsigned long)bits,
                         (unsigned int)s_font->width, (unsigned int)s_font->height,
                         (unsigned int)s_font->glyph_size);
            }

            // 显示缓存统计
            uint32_t hits = 0, misses = 0;
            font_cache_get_stats(&hits, &misses, NULL);
            ESP_LOGI(TAG, "Cache stats after probe: hits=%lu, misses=%lu", 
                     (unsigned long)hits, (unsigned long)misses);

            return true;
        }
    }

    // 兜底：在 8.3 短文件名环境下（或编码不一致），直接扫描 /sdcard/fonts 选择匹配的 raw 字体。
    if (try_load_font_by_scanning_dir("/sdcard/fonts")) {
        return true;
    }

    ESP_LOGE(TAG, "Failed to load any font!");
    return false;
}

bool xt_eink_font_has_char(uint32_t ch)
{
    if (s_font == NULL) {
        return false;
    }

    // 使用位图回调来检查字符是否存在
    // 如果返回非 NULL 指针，说明字符存在
    const uint8_t *bitmap = xt_eink_font_get_bitmap(s_font, ch);
    return bitmap != NULL;
}

bool xt_eink_font_get_glyph(uint32_t ch, xt_eink_glyph_t *glyph)
{
    if (s_font == NULL || glyph == NULL) {
        return false;
    }

    // 获取位图
    const uint8_t *bitmap = xt_eink_font_get_bitmap(s_font, ch);
    if (bitmap == NULL) {
        return false;
    }

    // 复制到位图缓冲区
    size_t glyph_size = s_font->glyph_size;
    if (glyph_size > sizeof(s_glyph_buffer)) {
        glyph_size = sizeof(s_glyph_buffer);
    }
    memcpy(s_glyph_buffer, bitmap, glyph_size);

    // 设置字形信息
    glyph->bitmap = s_glyph_buffer;
    glyph->width = s_font->width;
    glyph->height = s_font->height;

    return true;
}

int xt_eink_font_render_char(int x, int y, uint32_t ch, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (s_font == NULL || framebuffer == NULL) {
        return 0;
    }

    // 获取位图
    const uint8_t *bitmap = xt_eink_font_get_bitmap(s_font, ch);
    if (bitmap == NULL) {
        return s_font->width;
    }

    uint8_t width = s_font->width;
    uint8_t height = s_font->height;
    uint8_t stride = (width + 7) / 8;

    // 渲染位图到帧缓冲
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int byte_idx = row * stride + col / 8;
            int bit_idx = 7 - (col % 8);
            bool pixel = (bitmap[byte_idx] >> bit_idx) & 1;

            int px = x + col;
            int py = y + row;

            // 检查边界
            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height) {
                int byte_pos = py * ((fb_width + 7) / 8) + px / 8;
                int bit_pos = 7 - (px % 8);

                if (pixel) {
                    // 帧缓冲约定（与 EPD/GUI_Paint 一致）：bit=0 表示黑，bit=1 表示白。
                    // 调用方通常传入 COLOR_BLACK(0x00) / COLOR_WHITE(0xFF)。
                    if (color == 0x00) {
                        // 黑色：清零该位
                        framebuffer[byte_pos] &= ~(1 << bit_pos);
                    } else {
                        // 白色：置位该位
                        framebuffer[byte_pos] |= (1 << bit_pos);
                    }
                }
            }
        }
    }

    return width;
}

int xt_eink_font_render_text(int x, int y, const char *text, uint8_t color,
                             uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (text == NULL || framebuffer == NULL) {
        return 0;
    }

    int current_x = x;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        int char_width = xt_eink_font_render_char(current_x, y, ch, color,
                                                   framebuffer, fb_width, fb_height);
        current_x += char_width;
        p += offset;
    }

    return current_x - x;
}

int xt_eink_font_get_text_width(const char *text)
{
    if (s_font == NULL || text == NULL) {
        return 0;
    }

    int width = 0;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        // XTEinkFontBinary 为固定宽度字形
        width += s_font->width;

        p += offset;
    }

    return width;
}

int xt_eink_font_get_height(void)
{
    if (s_font == NULL) {
        return 0;
    }
    return s_font->height;
}
