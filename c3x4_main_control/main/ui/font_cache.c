/**
 * @file font_cache.c
 * @brief 字体智能缓存实现（优先 Flash，未命中 SD 卡）
 */

#include "font_cache.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "FONT_CACHE";

// 字体参数（当前工程默认使用 19×25 字体）
// 注意：xt_eink_font 侧可以推断其他尺寸，但 font_cache 目前仍按固定字节数缓存。
#define FONT_WIDTH 19
#define FONT_HEIGHT 25
#define FONT_WIDTH_BYTES ((FONT_WIDTH + 7) / 8)  // 3 字节
#define GLYPH_SIZE (FONT_WIDTH_BYTES * FONT_HEIGHT)  // 75 字节

// 缓存路径
#define CACHE_DIR "/littlefs/fonts"

// 默认：缓存 U+0000..U+0BB7（共 3000 个）。
#define RANGE_CACHE_START 0x0000u
#define RANGE_CACHE_COUNT 3000u

// 可选：使用固件内置字表（例如《通用规范汉字表》一级 3500 + 标点）
// 通过生成的头文件提供 FONT_CACHE_LEVEL1_TABLE_COUNT 与 g_font_cache_level1_table[]
#include "font_cache_level1_table.h"

#define CACHE_FILE_RANGE "/littlefs/fonts/range_u0000_u0bb7.bin"
#define CACHE_FILE_TABLE "/littlefs/fonts/level1_table.bin"

// 统计信息
static uint32_t s_cache_hits = 0;
static uint32_t s_cache_misses = 0;
static uint32_t s_cached_chars = 0;

// SD 卡字体文件句柄
static FILE *s_sd_font_file = NULL;
static char s_sd_font_path[128] = {0};
// LittleFS 缓存文件句柄
static FILE *s_cache_file = NULL;

typedef enum {
    FONT_CACHE_MODE_RANGE = 0,
    FONT_CACHE_MODE_TABLE = 1,
} font_cache_mode_t;

static font_cache_mode_t s_mode = FONT_CACHE_MODE_RANGE;
static uint32_t s_active_count = 0;
static const char *s_cache_path = NULL;

// 缓存文件格式：header + (可选 codepoint table) + glyph data
#define FONT_CACHE_MAGIC 0x46434B31u  // 'FCK1'
#define FONT_CACHE_VERSION 1u
#define FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE (1u << 0)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t glyph_size;
    uint32_t count;
    uint32_t flags;
} font_cache_file_header_t;

static bool ensure_cache_dir(void)
{
    struct stat st;
    if (stat(CACHE_DIR, &st) == 0) {
        return true;
    }
    if (mkdir(CACHE_DIR, 0755) != 0) {
        ESP_LOGE(TAG, "Failed to create cache dir: %s (errno=%d)", CACHE_DIR, errno);
        return false;
    }
    return true;
}

static bool read_cache_header(FILE *f, font_cache_file_header_t *out)
{
    if (f == NULL || out == NULL) {
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        return false;
    }
    size_t n = fread(out, 1, sizeof(*out), f);
    if (n != sizeof(*out)) {
        return false;
    }
    if (out->magic != FONT_CACHE_MAGIC || out->version != FONT_CACHE_VERSION) {
        return false;
    }
    return true;
}

static int table_binary_search_u16(const uint16_t *arr, uint32_t n, uint16_t key)
{
    uint32_t lo = 0;
    uint32_t hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint16_t v = arr[mid];
        if (v == key) {
            return (int)mid;
        }
        if (v < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return -1;
}

static bool read_from_cache(uint32_t unicode, uint8_t *buffer)
{
    if (s_cache_file == NULL) {
        return false;
    }

    font_cache_file_header_t hdr;
    if (!read_cache_header(s_cache_file, &hdr)) {
        return false;
    }
    if (hdr.glyph_size != GLYPH_SIZE) {
        return false;
    }

    uint32_t index = UINT32_MAX;
    uint32_t table_bytes = 0;

    if (s_mode == FONT_CACHE_MODE_TABLE) {
        if ((hdr.flags & FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE) == 0) {
            return false;
        }
        if (FONT_CACHE_LEVEL1_TABLE_COUNT == 0) {
            return false;
        }
        if (unicode > 0xFFFFu) {
            return false;
        }
        int found = table_binary_search_u16(g_font_cache_level1_table, FONT_CACHE_LEVEL1_TABLE_COUNT, (uint16_t)unicode);
        if (found < 0) {
            return false;
        }
        index = (uint32_t)found;
        table_bytes = (uint32_t)hdr.count * sizeof(uint16_t);
    } else {
        // range mode
        if ((hdr.flags & FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE) != 0) {
            return false;
        }
        uint32_t rel = unicode - RANGE_CACHE_START;
        if (rel >= hdr.count) {
            return false;
        }
        index = rel;
        table_bytes = 0;
    }

    uint32_t data_base = (uint32_t)sizeof(font_cache_file_header_t) + table_bytes;
    uint32_t offset = data_base + index * GLYPH_SIZE;
    if (fseek(s_cache_file, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    size_t read_bytes = fread(buffer, 1, GLYPH_SIZE, s_cache_file);
    return read_bytes == GLYPH_SIZE;
}

/**
 * @brief 从 SD 卡完整字体读取字形
 */
static bool read_from_sd(uint32_t unicode, uint8_t *buffer) {
    if (s_sd_font_file == NULL) {
        return false;
    }

    // XTEinkFontBinary 格式：固定 0x10000 个字形，无文件头
    if (unicode >= 0x10000) {
        return false;
    }

    // 定位并读取字形
    long offset = (long)unicode * GLYPH_SIZE;
    if (fseek(s_sd_font_file, offset, SEEK_SET) != 0) {
        return false;
    }

    size_t read_bytes = fread(buffer, 1, GLYPH_SIZE, s_sd_font_file);
    return read_bytes == GLYPH_SIZE;
}

static bool generate_cache(const char *sd_font_path)
{
    if (!ensure_cache_dir()) {
        return false;
    }

    ESP_LOGI(TAG, "Generating font cache (%s) from: %s",
             (s_mode == FONT_CACHE_MODE_TABLE) ? "table" : "range", sd_font_path);

    FILE *src = fopen(sd_font_path, "rb");
    if (src == NULL) {
        ESP_LOGE(TAG, "Failed to open source font: %s (errno=%d)", sd_font_path, errno);
        return false;
    }

    // 检查文件大小（fontdecode.cs 格式：固定 0x10000 字形，无文件头）
    fseek(src, 0, SEEK_END);
    long file_size = ftell(src);
    fseek(src, 0, SEEK_SET);
    long expected_size = 0x10000L * GLYPH_SIZE;
    if (file_size != expected_size) {
        ESP_LOGW(TAG, "Font file size mismatch: got %ld, expected %ld", file_size, expected_size);
    }

    const uint32_t count = (s_mode == FONT_CACHE_MODE_TABLE) ? (uint32_t)FONT_CACHE_LEVEL1_TABLE_COUNT
                                                             : (uint32_t)RANGE_CACHE_COUNT;
    const uint32_t flags = (s_mode == FONT_CACHE_MODE_TABLE) ? FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE : 0u;

    FILE *dst = fopen(s_cache_path, "wb");
    if (dst == NULL) {
        ESP_LOGE(TAG, "Failed to create cache file: %s (errno=%d)", s_cache_path, errno);
        fclose(src);
        return false;
    }

    font_cache_file_header_t hdr = {
        .magic = FONT_CACHE_MAGIC,
        .version = FONT_CACHE_VERSION,
        .glyph_size = GLYPH_SIZE,
        .count = count,
        .flags = flags,
    };

    if (fwrite(&hdr, 1, sizeof(hdr), dst) != sizeof(hdr)) {
        ESP_LOGE(TAG, "Failed to write cache header");
        fclose(src);
        fclose(dst);
        return false;
    }

    if (flags & FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE) {
        // 写入 codepoint table（uint16_t）
        // 约 3500 * 2 = 7KB，写入开销很小。
        if (FONT_CACHE_LEVEL1_TABLE_COUNT == 0) {
            ESP_LOGE(TAG, "Table mode enabled but table is empty. Regenerate font_cache_level1_table.h");
            fclose(src);
            fclose(dst);
            return false;
        }
        if (fwrite(g_font_cache_level1_table, sizeof(uint16_t), FONT_CACHE_LEVEL1_TABLE_COUNT, dst) != FONT_CACHE_LEVEL1_TABLE_COUNT) {
            ESP_LOGE(TAG, "Failed to write codepoint table");
            fclose(src);
            fclose(dst);
            return false;
        }
    }

    uint8_t glyph_buffer[GLYPH_SIZE];
    uint8_t zero_buffer[GLYPH_SIZE];
    memset(zero_buffer, 0, sizeof(zero_buffer));
    uint32_t written = 0;
    uint32_t bad = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t unicode = 0;
        if (s_mode == FONT_CACHE_MODE_TABLE) {
            unicode = (uint32_t)g_font_cache_level1_table[i];
        } else {
            unicode = RANGE_CACHE_START + i;
        }

        bool ok = false;
        if (unicode < 0x10000u) {
            long offset = (long)unicode * GLYPH_SIZE;
            if (fseek(src, offset, SEEK_SET) == 0) {
                if (fread(glyph_buffer, 1, GLYPH_SIZE, src) == GLYPH_SIZE) {
                    ok = true;
                }
            }
        }

        const uint8_t *to_write = ok ? glyph_buffer : zero_buffer;
        if (!ok) {
            bad++;
        }
        if (fwrite(to_write, 1, GLYPH_SIZE, dst) != GLYPH_SIZE) {
            ESP_LOGE(TAG, "Write failed at index=%" PRIu32 " U+%04" PRIX32, i, unicode);
            break;
        }
        written++;
    }

    fclose(src);
    fclose(dst);

    ESP_LOGI(TAG, "Cache generated: count=%" PRIu32 " glyph=%u bytes, bad=%" PRIu32, written, (unsigned)GLYPH_SIZE, bad);
    s_cached_chars = written;
    return (written == count);
}

// ========== 公共接口 ==========

bool font_cache_init(const char *sd_font_path)
{
    if (sd_font_path == NULL) {
        ESP_LOGE(TAG, "Invalid SD font path");
        return false;
    }

    // 保存路径
    strncpy(s_sd_font_path, sd_font_path, sizeof(s_sd_font_path) - 1);
    s_sd_font_path[sizeof(s_sd_font_path) - 1] = '\0';

    // 选择模式：若固件内置表非空，则用 table；否则用 range。
    if (FONT_CACHE_LEVEL1_TABLE_COUNT > 0) {
        s_mode = FONT_CACHE_MODE_TABLE;
        s_active_count = (uint32_t)FONT_CACHE_LEVEL1_TABLE_COUNT;
        s_cache_path = CACHE_FILE_TABLE;
    } else {
        s_mode = FONT_CACHE_MODE_RANGE;
        s_active_count = (uint32_t)RANGE_CACHE_COUNT;
        s_cache_path = CACHE_FILE_RANGE;
    }

    // 打开 SD 卡字体文件（用于缓存未命中时读取）
    if (s_sd_font_file != NULL) {
        fclose(s_sd_font_file);
        s_sd_font_file = NULL;
    }
    s_sd_font_file = fopen(sd_font_path, "rb");
    if (s_sd_font_file == NULL) {
        ESP_LOGE(TAG, "Failed to open SD font: %s (errno=%d)", sd_font_path, errno);
        return false;
    }

    // 若已有缓存文件且 header 匹配则复用，否则重建。
    bool need_generate = true;
    struct stat st;
    if (stat(s_cache_path, &st) == 0 && st.st_size > (long)sizeof(font_cache_file_header_t)) {
        FILE *tmp = fopen(s_cache_path, "rb");
        if (tmp != NULL) {
            font_cache_file_header_t hdr;
            if (read_cache_header(tmp, &hdr)) {
                uint32_t expect_flags = (s_mode == FONT_CACHE_MODE_TABLE) ? FONT_CACHE_FLAG_HAS_CODEPOINT_TABLE : 0u;
                if (hdr.glyph_size == GLYPH_SIZE && hdr.count == s_active_count && hdr.flags == expect_flags) {
                    need_generate = false;
                    s_cached_chars = hdr.count;
                }
            }
            fclose(tmp);
        }
    }

    if (need_generate) {
        ESP_LOGI(TAG, "Cache not found or mismatch, generating: %s", s_cache_path);
        if (!generate_cache(sd_font_path)) {
            ESP_LOGE(TAG, "Failed to generate cache");
            return false;
        }
        s_cached_chars = s_active_count;
    } else {
        ESP_LOGI(TAG, "Cache found: %s (%ld bytes)", s_cache_path, st.st_size);
    }

    if (s_cache_file != NULL) {
        fclose(s_cache_file);
        s_cache_file = NULL;
    }
    s_cache_file = fopen(s_cache_path, "rb");
    if (s_cache_file == NULL) {
        ESP_LOGE(TAG, "Failed to open cache file: %s (errno=%d)", s_cache_path, errno);
        return false;
    }

    ESP_LOGI(TAG, "Font cache initialized: mode=%s cached=%" PRIu32 " file=%s",
             (s_mode == FONT_CACHE_MODE_TABLE) ? "table" : "range", s_cached_chars, s_cache_path);
    return true;
}

int font_cache_get_glyph(uint32_t unicode, uint8_t *buffer, size_t buf_size) {
    if (buffer == NULL || buf_size < GLYPH_SIZE) {
        return 0;
    }

    // 1) 尝试从 LittleFS 缓存读取
    if (read_from_cache(unicode, buffer)) {
        s_cache_hits++;
        return GLYPH_SIZE;
    }

    // 2) 未命中：从 SD 卡读取
    s_cache_misses++;
    if (read_from_sd(unicode, buffer)) {
        return GLYPH_SIZE;
    }

    return 0;
}

void font_cache_get_stats(uint32_t *hits, uint32_t *misses, uint32_t *cached_chars) {
    if (hits != NULL) *hits = s_cache_hits;
    if (misses != NULL) *misses = s_cache_misses;
    if (cached_chars != NULL) *cached_chars = s_cached_chars;
}

void font_cache_cleanup(void) {
    if (s_cache_file != NULL) {
        fclose(s_cache_file);
        s_cache_file = NULL;
    }
    if (s_sd_font_file != NULL) {
        fclose(s_sd_font_file);
        s_sd_font_file = NULL;
    }
    s_cache_hits = 0;
    s_cache_misses = 0;
}
