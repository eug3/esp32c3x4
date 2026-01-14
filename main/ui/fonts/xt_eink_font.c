/**
 * @file xt_eink_font.c
 * @brief XTEinkFontBinary 字体解析器实现
 */

#include "xt_eink_font.h"
#include "font_cache.h"
#include "font_partition.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "XT_EINK_FONT";

// fontdecode.cs 格式：固定 0x10000 个字形（U+0000..U+FFFF），无文件头
#define XT_EINK_TOTAL_CHARS 0x10000u

// SD卡访问状态缓存（避免重复检查）
static bool s_sdcard_checked = false;
static bool s_sdcard_accessible = false;

// 字形位图缓冲区（临时缓存，用于从文件读取）
static uint8_t *g_glyph_buffer = NULL;
static size_t g_glyph_buffer_size = 0;

// 字形描述符（静态分配，避免频繁分配）
static xt_eink_font_glyph_dsc_t g_glyph_dsc;

static void dump_dir_limited(const char *path, int max_entries)
{
    if (path == NULL || max_entries <= 0) {
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        int err = errno;
        ESP_LOGW(TAG, "opendir('%s') failed: errno=%d (%s)", path, err, strerror(err));
        return;
    }

    ESP_LOGI(TAG, "Directory listing: %s", path);
    struct dirent *ent = NULL;
    int n = 0;
    while ((ent = readdir(dir)) != NULL && n < max_entries) {
        if (ent->d_name[0] == '\0') {
            continue;
        }
        ESP_LOGI(TAG, "  - %s", ent->d_name);
        n++;
    }
    closedir(dir);
}

static void log_stat_result(const char *path)
{
    if (path == NULL) {
        return;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "stat('%s') ok: mode=0x%lx size=%lu", path,
                 (unsigned long)st.st_mode, (unsigned long)st.st_size);
        return;
    }

    int err = errno;
    ESP_LOGW(TAG, "stat('%s') failed: errno=%d (%s)", path, err, strerror(err));
}

static const char *get_basename(const char *path)
{
    if (path == NULL) {
        return "";
    }
    const char *last_slash = strrchr(path, '/');
    return (last_slash != NULL) ? (last_slash + 1) : path;
}

static bool parse_font_hint_size_from_path(const char *path, uint16_t *out_size)
{
    if (path == NULL || out_size == NULL) {
        return false;
    }

    const char *base = get_basename(path);
    const char *dot = strrchr(base, '.');
    size_t name_len = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (name_len == 0) {
        return false;
    }

    char name[128];
    if (name_len >= sizeof(name)) {
        name_len = sizeof(name) - 1;
    }
    memcpy(name, base, name_len);
    name[name_len] = '\0';

    int i = (int)strlen(name) - 1;
    while (i >= 0 && (name[i] < '0' || name[i] > '9')) {
        i--;
    }
    if (i < 0) {
        return false;
    }
    while (i >= 0 && (name[i] >= '0' && name[i] <= '9')) {
        i--;
    }

    const char *num = &name[i + 1];
    uint32_t size = (uint32_t)strtoul(num, NULL, 10);
    if (size == 0 || size > 255) {
        return false;
    }
    *out_size = (uint16_t)size;
    return true;
}

static bool infer_font_dimensions_from_file_size(uint32_t file_size, uint16_t hint_size,
                                                 uint16_t *out_w, uint16_t *out_h)
{
    if (out_w == NULL || out_h == NULL) {
        return false;
    }

    // fontdecode.cs：总字形固定 0x10000，每字形字节数应为整数。
    if (file_size == 0 || (file_size % XT_EINK_TOTAL_CHARS) != 0) {
        return false;
    }

    uint32_t char_byte = file_size / XT_EINK_TOTAL_CHARS;
    if (char_byte == 0 || char_byte > 4096) {
        return false;
    }

    // 常用候选（优先匹配）
    // 注意：优先选宽度是 8 的倍数的尺寸，避免把实际占用的位列截断。
    // 即使真实字模宽度是 14/15，这类 raw 格式也会按 widthByte=2（16位）存储；
    // 固件用 16 作为渲染宽度只会多出空白列，更安全。
    static const struct { uint16_t w; uint16_t h; } candidates[] = {
        { 8, 16 },
        { 16, 12 },
        { 16, 14 },
        { 16, 16 },
        { 16, 20 },
        { 24, 24 },
        { 32, 32 },
    };

    int best_idx = -1;
    uint32_t best_score = UINT32_MAX;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint32_t width_byte = (candidates[i].w + 7u) / 8u;
        uint32_t glyph_size = width_byte * (uint32_t)candidates[i].h;
        if (glyph_size != char_byte) {
            continue;
        }
        uint32_t score = 0;
        if (hint_size != 0) {
            uint32_t dh = (candidates[i].h > hint_size) ? (candidates[i].h - hint_size) : (hint_size - candidates[i].h);
            score += dh;
        }
        // 更偏好接近正方形
        uint32_t dwh = (candidates[i].w > candidates[i].h) ? (candidates[i].w - candidates[i].h)
                                                          : (candidates[i].h - candidates[i].w);
        score += dwh;
        if (score < best_score) {
            best_score = score;
            best_idx = (int)i;
        }
    }
    if (best_idx >= 0) {
        *out_w = candidates[best_idx].w;
        *out_h = candidates[best_idx].h;
        return true;
    }

    // 兜底：从 char_byte 分解出 height 与 widthByte，width 取 widthByte*8
    int best_found = 0;
    uint16_t best_w = 0;
    uint16_t best_h = 0;
    uint32_t best_fallback_score = UINT32_MAX;
    for (uint16_t h = 1; h <= 255; h++) {
        if ((char_byte % h) != 0) {
            continue;
        }
        uint32_t width_byte = char_byte / h;
        if (width_byte == 0 || width_byte > 32) {
            continue;
        }
        uint16_t w = (uint16_t)(width_byte * 8u);
        uint32_t score = 0;
        if (hint_size != 0) {
            uint32_t dh = (h > hint_size) ? (h - hint_size) : (hint_size - h);
            score += dh;
        }
        uint32_t dwh = (w > h) ? (w - h) : (h - w);
        score += dwh;

        if (!best_found || score < best_fallback_score) {
            best_found = 1;
            best_fallback_score = score;
            best_w = w;
            best_h = h;
        }
    }
    if (!best_found) {
        return false;
    }

    *out_w = best_w;
    *out_h = best_h;
    return true;
}

static bool parse_font_dimensions_from_path(const char *path, uint16_t *out_w, uint16_t *out_h)
{
    if (path == NULL || out_w == NULL || out_h == NULL) {
        return false;
    }

    // 仅用文件名推断：
    // 1) 支持 "... 16x20.bin" / "... 16×20.bin" / "...16X20.bin"
    // 2) 支持 "msyh-14.bin" 这种，只取末尾数字，默认 w=h=size

    const char *base = get_basename(path);
    const char *dot = strrchr(base, '.');
    size_t name_len = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (name_len == 0) {
        return false;
    }

    char name[128];
    if (name_len >= sizeof(name)) {
        name_len = sizeof(name) - 1;
    }
    memcpy(name, base, name_len);
    name[name_len] = '\0';

    // 尝试找 "x" / "X"
    char *sep = strrchr(name, 'x');
    if (sep == NULL) {
        sep = strrchr(name, 'X');
    }

    // 尝试找 UTF-8 的 "×"
    if (sep == NULL) {
        char *mul = strstr(name, "×");
        if (mul != NULL) {
            sep = mul; // 指向多字节起始
        }
    }

    if (sep != NULL) {
        // 找到分隔符，解析左右数字
        // 左侧数字起点：从 sep 往左跳过非数字，再继续往左找到数字段的起点
        char *l_end = sep;
        while (l_end > name && (l_end[-1] < '0' || l_end[-1] > '9')) {
            l_end--;
        }
        char *l_start = l_end;
        while (l_start > name && (l_start[-1] >= '0' && l_start[-1] <= '9')) {
            l_start--;
        }

        // 右侧数字起点：跳过分隔符本身（可能是 1 字节 x/X 或 2 字节 ×）
        char *r_start = sep;
        if (*r_start == 'x' || *r_start == 'X') {
            r_start++;
        } else {
            // "×" 的 UTF-8 是两个字节
            r_start += strlen("×");
        }
        while (*r_start != '\0' && (*r_start < '0' || *r_start > '9')) {
            r_start++;
        }

        char *r_end = r_start;
        while (*r_end >= '0' && *r_end <= '9') {
            r_end++;
        }

        if (l_start < l_end && r_start < r_end) {
            uint32_t w = (uint32_t)strtoul(l_start, NULL, 10);
            uint32_t h = (uint32_t)strtoul(r_start, NULL, 10);
            if (w > 0 && w <= 255 && h > 0 && h <= 255) {
                *out_w = (uint16_t)w;
                *out_h = (uint16_t)h;
                return true;
            }
        }
    }

    // 兜底：取末尾数字，例如 "msyh-14" -> 14
    int i = (int)strlen(name) - 1;
    while (i >= 0 && (name[i] < '0' || name[i] > '9')) {
        i--;
    }
    if (i < 0) {
        return false;
    }
    while (i >= 0 && (name[i] >= '0' && name[i] <= '9')) {
        i--;
    }
    const char *num = &name[i + 1];
    uint32_t size = (uint32_t)strtoul(num, NULL, 10);
    if (size == 0 || size > 255) {
        return false;
    }
    *out_w = (uint16_t)size;
    *out_h = (uint16_t)size;
    return true;
}

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
            ESP_LOGE(TAG, "Failed to allocate glyph buffer (%u bytes)", (unsigned int)size);
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
 * @brief 从文件读取字形位图（使用智能缓存）
 */
static bool read_glyph_from_file(xt_eink_font_t *font, uint32_t unicode, uint8_t *bitmap)
{
    // 检查 Unicode 范围
    if (unicode < font->header.first_char || unicode > font->header.last_char) {
        return false;
    }

    // 分区模式：直接从 Flash 分区读取，不使用缓存
    if (font->_use_partition && font_partition_is_available()) {
        size_t bytes_read = font_partition_read_glyph(unicode, bitmap, font->header.glyph_size);
        if (bytes_read > 0) {
            return true;
        }
        ESP_LOGE(TAG, "Font partition read failed for U+%04X", unicode);
        return false;
    }

    // 普通文件模式：使用智能缓存系统（优先 LittleFS，未命中则 SD 卡）。
    // 缓存仅对“当前已 init 的用户字体”生效，且要求 glyph_size 匹配。
    if (font_cache_is_enabled() && font_cache_get_active_glyph_size() == font->header.glyph_size) {
        int bytes_read = font_cache_get_glyph(unicode, bitmap, font->header.glyph_size);
        if (bytes_read > 0) {
            return true;
        }

        // table 缓存未覆盖的字很常见（未命中后会走直读），避免刷屏 warning。
        ESP_LOGD(TAG, "Font cache miss for U+%04X, fallback to direct file read", unicode);
    }

    // 最终兜底：直接从原文件读取（需要 SD 卡）
    if (font->fp == NULL) {
        ESP_LOGW(TAG, "No file handle and partition unavailable for U+%04X", unicode);
        return false;
    }
    
    // fontdecode.cs 格式：无文件头，直接按 unicode 索引
    uint32_t glyph_index = unicode - font->header.first_char;
    uint32_t offset = glyph_index * font->header.glyph_size;

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

static inline void set_bitmap_pixel_1bpp(uint8_t *buf, uint16_t width, uint16_t x, uint16_t y)
{
    uint16_t stride = (uint16_t)((width + 7u) / 8u);
    uint32_t idx = (uint32_t)y * (uint32_t)stride + (uint32_t)(x / 8u);
    uint8_t bit = (uint8_t)(7u - (x % 8u));
    buf[idx] |= (uint8_t)(1u << bit);
}

static void fill_missing_glyph_square(xt_eink_font_t *font, uint8_t *buf)
{
    if (font == NULL || buf == NULL) {
        return;
    }
    if (font->width == 0 || font->height == 0 || font->glyph_size == 0) {
        return;
    }

    memset(buf, 0x00, font->glyph_size);

    uint16_t w = (uint16_t)font->width;
    uint16_t h = (uint16_t)font->height;
    if (w < 2 || h < 2) {
        return;
    }

    // 边框 1px 方块
    for (uint16_t x = 0; x < w; x++) {
        set_bitmap_pixel_1bpp(buf, w, x, 0);
        set_bitmap_pixel_1bpp(buf, w, x, (uint16_t)(h - 1));
    }
    for (uint16_t y = 0; y < h; y++) {
        set_bitmap_pixel_1bpp(buf, w, 0, y);
        set_bitmap_pixel_1bpp(buf, w, (uint16_t)(w - 1), y);
    }
}

xt_eink_font_t *xt_eink_font_open(const char *path)
{
    if (path == NULL) {
        ESP_LOGE(TAG, "Font path is NULL");
        return NULL;
    }

    // 检查SD卡是否可访问（缓存结果，避免重复检查）
    if (!s_sdcard_checked) {
        struct stat st;
        s_sdcard_accessible = (stat("/sdcard", &st) == 0);
        s_sdcard_checked = true;
        if (!s_sdcard_accessible) {
            ESP_LOGW(TAG, "SD card not mounted at /sdcard - font loading will fail");
        }
    }

    // 检查文件是否存在
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        int err = errno;
        // 只输出错误信息，不做详细诊断（避免大量日志）
        ESP_LOGE(TAG, "Failed to open font file: %s (errno=%d: %s)", path, err, strerror(err));
        // 只在SD卡不可访问且是第一次打印详细信息时输出诊断
        if (!s_sdcard_accessible) {
            static bool first_diagnostic = true;
            if (first_diagnostic) {
                log_stat_result("/sdcard");
                log_stat_result("/sdcard/fonts");
                dump_dir_limited("/sdcard", 24);
                dump_dir_limited("/sdcard/fonts", 48);
                first_diagnostic = false;
            }
        } else {
            // SD卡可访问但文件不存在，输出路径检查
            log_stat_result(path);
        }
        return NULL;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Font file size invalid: %s (%ld)", path, file_size);
        fclose(fp);
        return NULL;
    }

    uint32_t u_file_size = (uint32_t)file_size;
    if ((u_file_size % XT_EINK_TOTAL_CHARS) != 0) {
        ESP_LOGE(TAG, "Font file size not divisible by 0x10000: %s (%lu)",
                 path, (unsigned long)u_file_size);
        fclose(fp);
        return NULL;
    }

    // fontdecode.cs 格式：从文件名解析宽高，并用文件大小校验
    uint16_t w = 0, h = 0;
    uint16_t hint_size = 0;
    (void)parse_font_hint_size_from_path(path, &hint_size);

    bool have_explicit_dims = parse_font_dimensions_from_path(path, &w, &h);
    uint32_t char_byte = u_file_size / XT_EINK_TOTAL_CHARS;
    if (have_explicit_dims) {
        uint32_t width_byte = (w + 7u) / 8u;
        uint32_t glyph_size = width_byte * h;
        if (glyph_size != char_byte) {
            ESP_LOGW(TAG, "Filename dims %ux%u do not match file layout (charByte=%lu); inferring from file size",
                     (unsigned int)w, (unsigned int)h, (unsigned long)char_byte);
            have_explicit_dims = false;
        }
    }

    if (!have_explicit_dims) {
        if (!infer_font_dimensions_from_file_size(u_file_size, hint_size, &w, &h)) {
            ESP_LOGE(TAG, "Cannot infer font size from file size: %s (%lu bytes)",
                     path, (unsigned long)u_file_size);
            fclose(fp);
            return NULL;
        }
    }

    uint32_t width_byte = (w + 7u) / 8u;
    // 重要：raw 字模按“字节对齐宽度”存储。
    // 文件名里的宽度可能是“可见宽度”(例如 27)，但实际每行占用的位宽是 width_byte*8(例如 32)。
    // 若按 27 渲染，会直接截掉最后 5 列，可能导致字形几乎为空（尤其当生成器右对齐/居中时）。
    // 因此这里把渲染宽度对齐到字节宽度，确保不会截断。
    uint16_t render_w = (uint16_t)(width_byte * 8u);
    if (render_w != w) {
        ESP_LOGI(TAG, "Align font width: %u -> %u (width_byte=%lu)",
                 (unsigned int)w, (unsigned int)render_w, (unsigned long)width_byte);
        w = render_w;
    }

    uint32_t glyph_size = width_byte * h;
    if (glyph_size != char_byte) {
        ESP_LOGE(TAG, "Inferred dims %ux%u inconsistent (glyphSize=%lu, charByte=%lu): %s",
                 (unsigned int)w, (unsigned int)h,
                 (unsigned long)glyph_size, (unsigned long)char_byte, path);
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

    // 初始化上下文（raw 格式）
    strncpy(font->file_path, path, sizeof(font->file_path) - 1);
    font->fp = fp;
    font->file_size = u_file_size;
    memset(&font->header, 0, sizeof(font->header));
    font->header.version = 0;
    font->header.width = (uint8_t)w;
    font->header.height = (uint8_t)h;
    font->header.bpp = 1;
    font->header.char_count = XT_EINK_TOTAL_CHARS;
    font->header.first_char = 0;
    font->header.last_char = 0xFFFF;
    font->header.glyph_size = glyph_size;
    font->header.file_size = font->file_size;

    font->width = w;
    font->height = h;
    font->glyph_size = (uint16_t)glyph_size;
    font->line_height = h;

    ESP_LOGI(TAG, "Font opened: %s", path);
    ESP_LOGI(TAG, "  Raw Size: %ux%u, bpp=%u", (unsigned int)w, (unsigned int)h, 1u);
    ESP_LOGI(TAG, "  Chars: U+0000 - U+FFFF (%lu chars)", (unsigned long)XT_EINK_TOTAL_CHARS);
    ESP_LOGI(TAG, "  Glyph size: %lu bytes (file charByte=%lu)",
             (unsigned long)glyph_size, (unsigned long)char_byte);

    return font;
}

xt_eink_font_t *xt_eink_font_open_partition(void)
{
    // 检查 Flash 分区是否可用
    if (!font_partition_is_available()) {
        ESP_LOGE(TAG, "Font partition is not available");
        return NULL;
    }

    // 标准菜单字体规格：19×25
    uint16_t w = 19;
    uint16_t h = 25;
    uint32_t width_byte = (w + 7u) / 8u;  // (19 + 7) / 8 = 3
    uint32_t glyph_size = width_byte * h;  // 3 * 25 = 75
    uint32_t file_size = glyph_size * XT_EINK_TOTAL_CHARS;  // 75 * 65536 = 4,915,200

    // 获取分区大小进行验证
    size_t part_size = 0;
    size_t part_offset = 0;
    font_partition_get_info(&part_size, &part_offset);
    
    if (part_size < file_size) {
        ESP_LOGE(TAG, "Font partition too small: %lu bytes (need %lu bytes)",
                 (unsigned long)part_size, (unsigned long)file_size);
        return NULL;
    }

    // 分配上下文
    xt_eink_font_t *font = (xt_eink_font_t *)malloc(sizeof(xt_eink_font_t));
    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to allocate font context for partition");
        return NULL;
    }

    memset(font, 0, sizeof(xt_eink_font_t));

    // 初始化上下文（来自 Flash 分区）
    strncpy(font->file_path, "[flash_partition:font_data]", sizeof(font->file_path) - 1);
    font->fp = NULL;  // 分区不使用文件指针
    font->file_size = file_size;
    font->_use_partition = true;  // 标记为使用分区模式
    
    memset(&font->header, 0, sizeof(font->header));
    font->header.version = 0;
    font->header.width = (uint8_t)w;
    font->header.height = (uint8_t)h;
    font->header.bpp = 1;
    font->header.char_count = XT_EINK_TOTAL_CHARS;
    font->header.first_char = 0;
    font->header.last_char = 0xFFFF;
    font->header.glyph_size = glyph_size;
    font->header.file_size = file_size;

    font->width = w;
    font->height = h;
    font->glyph_size = (uint16_t)glyph_size;
    font->line_height = h;

    ESP_LOGI(TAG, "Font opened from partition: flash://font_data");
    ESP_LOGI(TAG, "  Size: %ux%u, bpp=%u", (unsigned int)w, (unsigned int)h, 1u);
    ESP_LOGI(TAG, "  Chars: U+0000 - U+FFFF (%lu chars)", (unsigned long)XT_EINK_TOTAL_CHARS);
    ESP_LOGI(TAG, "  Glyph size: %lu bytes", (unsigned long)glyph_size);

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

const xt_eink_font_glyph_dsc_t *xt_eink_font_get_glyph_dsc(xt_eink_font_t *font,
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
        ESP_LOGW(TAG, "Font height mismatch: requested=%lu, actual=%u",
                 (unsigned long)font_height, (unsigned int)font->height);
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
    if (font == NULL) {
        return NULL;
    }

    // 分区模式允许 fp 为空
    if (!font->_use_partition && font->fp == NULL) {
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

    // 从分区或文件读取字形；失败则返回方块占位
    if (!read_glyph_from_file(font, unicode, g_glyph_buffer)) {
        fill_missing_glyph_square(font, g_glyph_buffer);
        return g_glyph_buffer;
    }

    // 添加到缓存
    cache_glyph(font, unicode, g_glyph_buffer, font->glyph_size);

    return g_glyph_buffer;
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
                 "  Chars: %lu (U+%04lX - U+%04lX)\n"
                 "  Glyph size: %lu bytes\n"
                 "  Cache: %lu/%u (hit=%lu, miss=%lu)",
             font->file_path,
             font->width, font->height,
                 (unsigned long)font->header.char_count,
                 (unsigned long)font->header.first_char, (unsigned long)font->header.last_char,
                 (unsigned long)font->header.glyph_size,
                 (unsigned long)(font->cache_hit + font->cache_miss > 0
                     ? (font->cache_hit * 100UL) / (font->cache_hit + font->cache_miss)
                     : 0),
             XT_EINK_GLYPH_CACHE_SIZE,
                 (unsigned long)font->cache_hit, (unsigned long)font->cache_miss);
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

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fclose(fp);

    uint16_t w = 0, h = 0;
    if (!parse_font_dimensions_from_path(path, &w, &h)) {
        return false;
    }
    uint32_t width_byte = (w + 7u) / 8u;
    uint32_t glyph_size = width_byte * h;
    uint32_t expected_size = glyph_size * XT_EINK_TOTAL_CHARS;
    return (file_size >= 0) && ((uint32_t)file_size >= expected_size);
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
