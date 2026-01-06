/**
 * @file font_cache.c
 * @brief 字体智能缓存实现（优先 Flash，未命中 SD 卡）
 */

#include "font_cache.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "FONT_CACHE";

// 说明：缓存按“当前用户字体”的 glyph_size 生成/读取。
// 默认/菜单字体按需求不使用缓存（由上层不调用 font_cache_init() 来保证）。

// 缓存路径
#define CACHE_DIR "/littlefs/fonts"

// 默认：缓存 U+0000..U+0BB7（共 3000 个）。
#define RANGE_CACHE_START 0x0000u
#define RANGE_CACHE_COUNT 3000u

// 可选：使用固件内置字表（例如《通用规范汉字表》一级 3500 + 标点）
// 通过生成的头文件提供 FONT_CACHE_LEVEL1_TABLE_COUNT 与 g_font_cache_level1_table[]
#include "font_cache_level1_table.h"

// 旧版本缓存文件名（兼容清理用）
#define LEGACY_CACHE_FILE_RANGE "range_u0000_u0bb7.bin"
#define LEGACY_CACHE_FILE_TABLE "level1_table.bin"

// 新版本：用户字体缓存文件名前缀
#define USER_CACHE_PREFIX "ucache_"

// 统计信息
static uint32_t s_cache_hits = 0;
static uint32_t s_cache_misses = 0;
static uint32_t s_cached_chars = 0;

// SD 卡字体文件句柄
static FILE *s_sd_font_file = NULL;
static char s_sd_font_path[128] = {0};
// LittleFS 缓存文件句柄
static FILE *s_cache_file = NULL;
static char s_cache_path_buf[160] = {0};
static size_t s_glyph_size = 0;

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

static uint32_t fnv1a_32(const char *s)
{
    uint32_t h = 2166136261u;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        h ^= (uint32_t)(*p);
        h *= 16777619u;
    }
    return h;
}

static bool ends_with(const char *s, const char *suffix)
{
    if (s == NULL || suffix == NULL) {
        return false;
    }
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (sl < su) {
        return false;
    }
    return memcmp(s + (sl - su), suffix, su) == 0;
}

static bool starts_with(const char *s, const char *prefix)
{
    if (s == NULL || prefix == NULL) {
        return false;
    }
    size_t pl = strlen(prefix);
    return memcmp(s, prefix, pl) == 0;
}

static void purge_old_font_cache_files(const char *keep_filename)
{
    DIR *dir = opendir(CACHE_DIR);
    if (dir == NULL) {
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        if (keep_filename != NULL && strcmp(name, keep_filename) == 0) {
            continue;
        }

        // 仅清理字体缓存相关文件
        bool should_delete = false;
        if (strcmp(name, LEGACY_CACHE_FILE_RANGE) == 0 || strcmp(name, LEGACY_CACHE_FILE_TABLE) == 0) {
            should_delete = true;
        } else if (starts_with(name, USER_CACHE_PREFIX) && ends_with(name, ".bin")) {
            should_delete = true;
        }

        if (!should_delete) {
            continue;
        }

        char fullpath[200];
        int n = snprintf(fullpath, sizeof(fullpath), "%s/%s", CACHE_DIR, name);
        if (n <= 0 || (size_t)n >= sizeof(fullpath)) {
            continue;
        }

        remove(fullpath);
    }

    closedir(dir);
}

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
    if (s_glyph_size == 0 || hdr.glyph_size != (uint16_t)s_glyph_size) {
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
    uint32_t offset = data_base + index * (uint32_t)s_glyph_size;
    if (fseek(s_cache_file, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    size_t read_bytes = fread(buffer, 1, s_glyph_size, s_cache_file);
    return read_bytes == s_glyph_size;
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

    if (s_glyph_size == 0) {
        return false;
    }

    // 定位并读取字形
    long offset = (long)unicode * (long)s_glyph_size;
    if (fseek(s_sd_font_file, offset, SEEK_SET) != 0) {
        return false;
    }

    size_t read_bytes = fread(buffer, 1, s_glyph_size, s_sd_font_file);
    return read_bytes == s_glyph_size;
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
    long expected_size = 0;
    if (s_glyph_size > 0) {
        expected_size = 0x10000L * (long)s_glyph_size;
        if (file_size != expected_size) {
            ESP_LOGW(TAG, "Font file size mismatch: got %ld, expected %ld", file_size, expected_size);
        }
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
        .glyph_size = (uint16_t)s_glyph_size,
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

    if (s_glyph_size == 0) {
        ESP_LOGE(TAG, "Invalid glyph_size=0");
        fclose(src);
        fclose(dst);
        return false;
    }

    uint8_t *glyph_buffer = (uint8_t *)malloc(s_glyph_size);
    uint8_t *zero_buffer = (uint8_t *)malloc(s_glyph_size);
    if (glyph_buffer == NULL || zero_buffer == NULL) {
        ESP_LOGE(TAG, "OOM allocating glyph buffers (%u bytes)", (unsigned)s_glyph_size);
        if (glyph_buffer) free(glyph_buffer);
        if (zero_buffer) free(zero_buffer);
        fclose(src);
        fclose(dst);
        return false;
    }
    memset(zero_buffer, 0, s_glyph_size);
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
            long offset = (long)unicode * (long)s_glyph_size;
            if (fseek(src, offset, SEEK_SET) == 0) {
                if (fread(glyph_buffer, 1, s_glyph_size, src) == s_glyph_size) {
                    ok = true;
                }
            }
        }

        const uint8_t *to_write = ok ? glyph_buffer : zero_buffer;
        if (!ok) {
            bad++;
        }
        if (fwrite(to_write, 1, s_glyph_size, dst) != s_glyph_size) {
            ESP_LOGE(TAG, "Write failed at index=%" PRIu32 " U+%04" PRIX32, i, unicode);
            break;
        }
        written++;
    }

    free(glyph_buffer);
    free(zero_buffer);

    fclose(src);
    fclose(dst);

    ESP_LOGI(TAG, "Cache generated: count=%" PRIu32 " glyph=%u bytes, bad=%" PRIu32, written, (unsigned)s_glyph_size, bad);
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

    // 计算 glyph_size（文件必须是 0x10000 个字形，无文件头）
    struct stat st_font;
    if (stat(sd_font_path, &st_font) != 0 || st_font.st_size <= 0) {
        ESP_LOGE(TAG, "Failed to stat SD font: %s (errno=%d)", sd_font_path, errno);
        return false;
    }
    long file_size = st_font.st_size;
    long expected_chars = 0x10000L;
    if ((file_size % expected_chars) != 0) {
        ESP_LOGW(TAG, "Unsupported font file size (not divisible by 65536): %s size=%ld", sd_font_path, file_size);
        return false;
    }
    long char_byte = file_size / expected_chars;
    if (char_byte <= 0 || char_byte > 4096) {
        ESP_LOGW(TAG, "Invalid glyph_size=%ld derived from file: %s", char_byte, sd_font_path);
        return false;
    }
    s_glyph_size = (size_t)char_byte;

    // 保存路径
    strncpy(s_sd_font_path, sd_font_path, sizeof(s_sd_font_path) - 1);
    s_sd_font_path[sizeof(s_sd_font_path) - 1] = '\0';

    // 生成“当前字体专属”缓存文件名，并清理历史缓存
    uint32_t h = fnv1a_32(sd_font_path);
    int nn = snprintf(s_cache_path_buf, sizeof(s_cache_path_buf), "%s/%s%08" PRIX32 "_g%u.bin",
                      CACHE_DIR, USER_CACHE_PREFIX, h, (unsigned)s_glyph_size);
    if (nn <= 0 || (size_t)nn >= sizeof(s_cache_path_buf)) {
        ESP_LOGE(TAG, "Cache path too long");
        return false;
    }
    s_cache_path = s_cache_path_buf;
    const char *keep_name = strrchr(s_cache_path_buf, '/');
    keep_name = (keep_name != NULL) ? (keep_name + 1) : s_cache_path_buf;

    // 选择模式：优先使用固件内置表（3555）以节省空间；否则用 range。
    if (FONT_CACHE_LEVEL1_TABLE_COUNT > 0) {
        s_mode = FONT_CACHE_MODE_TABLE;
        s_active_count = (uint32_t)FONT_CACHE_LEVEL1_TABLE_COUNT;
    } else {
        s_mode = FONT_CACHE_MODE_RANGE;
        s_active_count = (uint32_t)RANGE_CACHE_COUNT;
    }

    // 切换字体：关闭旧句柄并清理历史缓存文件
    if (s_cache_file != NULL) {
        fclose(s_cache_file);
        s_cache_file = NULL;
    }
    if (s_sd_font_file != NULL) {
        fclose(s_sd_font_file);
        s_sd_font_file = NULL;
    }
    purge_old_font_cache_files(keep_name);

    // 打开 SD 卡字体文件（用于缓存未命中时读取）
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
                if ((size_t)hdr.glyph_size == s_glyph_size && hdr.count == s_active_count && hdr.flags == expect_flags) {
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

    s_cache_file = fopen(s_cache_path, "rb");
    if (s_cache_file == NULL) {
        ESP_LOGE(TAG, "Failed to open cache file: %s (errno=%d)", s_cache_path, errno);
        return false;
    }

    ESP_LOGI(TAG, "Font cache initialized: mode=%s cached=%" PRIu32 " glyph=%u file=%s",
             (s_mode == FONT_CACHE_MODE_TABLE) ? "table" : "range", s_cached_chars, (unsigned)s_glyph_size, s_cache_path);
    return true;
}

int font_cache_get_glyph(uint32_t unicode, uint8_t *buffer, size_t buf_size) {
    // 缓存只对“当前已 init 的字体”有效：要求 buf_size 与当前 glyph_size 一致
    if (buffer == NULL || s_cache_file == NULL || s_glyph_size == 0 || buf_size != s_glyph_size) {
        return 0;
    }

    // 1) 尝试从 LittleFS 缓存读取
    if (read_from_cache(unicode, buffer)) {
        s_cache_hits++;
        return (int)s_glyph_size;
    }

    // 2) 未命中：从 SD 卡读取
    s_cache_misses++;
    if (read_from_sd(unicode, buffer)) {
        return (int)s_glyph_size;
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
    s_cached_chars = 0;
    s_glyph_size = 0;
    s_cache_path_buf[0] = '\0';
    s_cache_path = NULL;
}

bool font_cache_is_enabled(void)
{
    return (s_cache_file != NULL) && (s_glyph_size > 0);
}

size_t font_cache_get_active_glyph_size(void)
{
    return font_cache_is_enabled() ? s_glyph_size : 0;
}
