/**
 * @file xt_eink_font_impl.c
 * @brief XTEink 字体渲染实现
 */

#include "xt_eink_font_impl.h"
#include "xt_eink_font.h"
#include "font_cache.h"
#include "font_partition.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "XT_FONT_IMPL";

// NVS配置
static const char *NVS_NAMESPACE = "font_settings";
static const char *NVS_KEY_FONT_PATH = "font_path";

// 字体上下文
static xt_eink_font_t *s_font = NULL;

// 菜单默认字体上下文（不受用户字体切换影响）
static xt_eink_font_t *s_menu_default_font = NULL;

// 菜单默认字体路径（用于绑定默认缓存）
static char s_menu_font_path[128] = {0};

// 已加载字体路径（用于初始化缓存）
static char s_loaded_font_path[128] = {0};

// 字形缓冲区
static uint8_t s_glyph_buffer[256];  // 最大字形大小

static bool is_sdcard_available(void)
{
    struct stat st;
    if (stat("/sdcard", &st) != 0) {
        return false;
    }
    if (stat("/sdcard/fonts", &st) != 0) {
        return false;
    }
    return true;
}

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

static inline void fb_put_pixel(int x, int y, uint8_t color, uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (framebuffer == NULL) {
        return;
    }
    if (x < 0 || y < 0 || x >= fb_width || y >= fb_height) {
        return;
    }
    int byte_pos = y * ((fb_width + 7) / 8) + x / 8;
    int bit_pos = 7 - (x % 8);
    if (color == 0x00) {
        framebuffer[byte_pos] &= ~(1 << bit_pos);
    } else {
        framebuffer[byte_pos] |= (1 << bit_pos);
    }
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

/**
 * @brief 从NVS加载用户选择的字体路径
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return true 成功加载，false 无保存的设置
 */
static bool load_font_path_from_nvs(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = buffer_size;
    err = nvs_get_str(handle, NVS_KEY_FONT_PATH, buffer, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(TAG, "Failed to get font path from NVS: %s", esp_err_to_name(err));
        return false;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded font path from NVS: %s", buffer);
    return true;
}

/**
 * @brief 尝试打开指定路径的字体
 */
static bool try_open_font(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    ESP_LOGI(TAG, "Trying to load font: %s", path);
    s_font = xt_eink_font_open(path);
    if (s_font != NULL) {
        ESP_LOGI(TAG, "Font loaded successfully: %s", path);

        // 保存字体路径
        strncpy(s_loaded_font_path, path, sizeof(s_loaded_font_path) - 1);
        s_loaded_font_path[sizeof(s_loaded_font_path) - 1] = '\0';

        // 初始化用户字体缓存：为当前字体生成/复用 LittleFS 缓存；失败则回退为直接从 SD 读取。
        if (!font_cache_init(path)) {
            ESP_LOGW(TAG, "Font cache init failed, will use direct SD card access");
        } else {
            uint32_t cached_chars = 0;
            font_cache_get_stats(NULL, NULL, &cached_chars);
            ESP_LOGI(TAG, "Font cache ready: %lu common chars in Flash", (unsigned long)cached_chars);
        }

        // 自检：读取几个常见汉字的位图
        const uint32_t probe_chars[] = { 0x6587u /* 文 */, 0x8BBEu /* 设 */, 0x7F6Eu /* 置 */ };
        for (size_t k = 0; k < sizeof(probe_chars) / sizeof(probe_chars[0]); k++) {
            const uint8_t *bmp = xt_eink_font_get_bitmap(s_font, probe_chars[k]);
            if (bmp != NULL) {
                uint32_t bits = count_bits_set(bmp, s_font->glyph_size > 64 ? 64 : s_font->glyph_size);
                ESP_LOGI(TAG, "Probe U+%04lX bits_set=%lu", (unsigned long)probe_chars[k], (unsigned long)bits);
            }
        }

        return true;
    }

    return false;
}

bool xt_eink_font_init(void)
{
    // 注意：避免重复初始化导致内存泄漏
    // 但允许字体切换时重新加载
    
    // 初始化字体分区（默认字体来源）
    if (font_partition_init()) {
        ESP_LOGI(TAG, "Font partition initialized successfully");
    } else {
        ESP_LOGW(TAG, "Font partition not available");
    }
    
    // 0) 先初始化菜单默认字体（必须固定为启动默认 19x25 字体，不受 NVS/用户字体影响）
    if (s_menu_default_font == NULL) {
        // 菜单默认字体：只允许从 Flash 分区加载（前提是分区有效）
        if (font_partition_is_available() && font_partition_is_valid()) {
            xt_eink_font_t *menu_font = xt_eink_font_open_partition();
            if (menu_font != NULL) {
                s_menu_default_font = menu_font;
                strncpy(s_menu_font_path, "[font_data_partition]", sizeof(s_menu_font_path) - 1);
                s_menu_font_path[sizeof(s_menu_font_path) - 1] = '\0';
                ESP_LOGI(TAG, "Menu default font initialized from Flash partition");
            }
        }

        if (s_menu_default_font == NULL) {
            ESP_LOGE(TAG, "Menu default font not available: please flash valid font_data partition");
        }
    }

    // 1. 然后尝试从NVS加载用户选择的字体（仅影响阅读器字体）
    char saved_font_path[128] = {0};
    bool loaded = false;

    // 仅两种方式：用户字体（LittleFS 缓存 + SD 回源）或默认分区字体。
    // 因此，SD 卡不可用时不尝试打开用户字体，直接回退到默认分区字体。
    bool sd_ready = is_sdcard_available();
    
    // 如果已经加载了用户字体且与菜单字体不同，需要先释放
    if (s_font != NULL && s_font != s_menu_default_font) {
        ESP_LOGI(TAG, "Releasing previous user font before reload");
        xt_eink_font_close(s_font);
        s_font = NULL;
        memset(s_loaded_font_path, 0, sizeof(s_loaded_font_path));
    }
    
    if (!loaded) {
        if (sd_ready && load_font_path_from_nvs(saved_font_path, sizeof(saved_font_path))) {
            if (try_open_font(saved_font_path)) {
                loaded = true;
            } else {
                ESP_LOGW(TAG, "Saved user font open failed; falling back to partition default");
            }
        } else if (!sd_ready) {
            ESP_LOGW(TAG, "SD card not available; skip user font and use partition default");
        }
    }

    // 2. 若用户字体未加载成功，则回退到菜单默认字体
    if (!loaded) {
        if (s_menu_default_font != NULL) {
            s_font = s_menu_default_font;
            strncpy(s_loaded_font_path, s_menu_font_path, sizeof(s_loaded_font_path) - 1);
            s_loaded_font_path[sizeof(s_loaded_font_path) - 1] = '\0';
            loaded = true;
            ESP_LOGI(TAG, "Reader font fallback to menu default: %s", s_loaded_font_path);
        }
    }

    return true;
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
        // 缺字：画方块占位（边框）
        int w = s_font->width;
        int h = s_font->height;
        if (w <= 0 || h <= 0) {
            return s_font->width;
        }
        for (int col = 0; col < w; col++) {
            fb_put_pixel(x + col, y, color, framebuffer, fb_width, fb_height);
            fb_put_pixel(x + col, y + h - 1, color, framebuffer, fb_width, fb_height);
        }
        for (int row = 0; row < h; row++) {
            fb_put_pixel(x, y + row, color, framebuffer, fb_width, fb_height);
            fb_put_pixel(x + w - 1, y + row, color, framebuffer, fb_width, fb_height);
        }
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

const char *xt_eink_font_get_current_path(void)
{
    return s_loaded_font_path[0] != '\0' ? s_loaded_font_path : NULL;
}

void xt_eink_font_set_current_path(const char *path)
{
    if (path != NULL) {
        strncpy(s_loaded_font_path, path, sizeof(s_loaded_font_path) - 1);
        s_loaded_font_path[sizeof(s_loaded_font_path) - 1] = '\0';
    } else {
        s_loaded_font_path[0] = '\0';
    }
}

void *xt_eink_font_get_menu_default_font(void)
{
    // 返回菜单默认字体
    // 如果未初始化或当前没有菜单字体，使用当前加载的字体作为回退
    if (s_menu_default_font != NULL) {
        return (void *)s_menu_default_font;
    }
    
    // 回退：返回当前加载的字体（此时应该是启动时加载的默认字体）
    // 但要确保菜单字体和当前字体不同时，菜单使用旧的
    if (s_font != NULL) {
        return (void *)s_font;
    }
    
    return NULL;
}

bool xt_eink_font_menu_has_char(uint32_t ch)
{
    // 使用菜单默认字体而不是当前用户字体
    xt_eink_font_t *font = (xt_eink_font_t *)xt_eink_font_get_menu_default_font();
    if (font == NULL) {
        return false;
    }

    const uint8_t *bitmap = xt_eink_font_get_bitmap(font, ch);
    return bitmap != NULL;
}

bool xt_eink_font_menu_get_glyph(uint32_t ch, xt_eink_glyph_t *glyph)
{
    // 使用菜单默认字体而不是当前用户字体
    xt_eink_font_t *font = (xt_eink_font_t *)xt_eink_font_get_menu_default_font();
    if (font == NULL || glyph == NULL) {
        return false;
    }

    const uint8_t *bitmap = xt_eink_font_get_bitmap(font, ch);
    if (bitmap == NULL) {
        return false;
    }

    glyph->bitmap = (uint8_t *)bitmap;
    glyph->width = font->width;
    glyph->height = font->height;

    return true;
}

int xt_eink_font_menu_get_height(void)
{
    // 使用菜单默认字体而不是当前用户字体
    xt_eink_font_t *font = (xt_eink_font_t *)xt_eink_font_get_menu_default_font();
    if (font == NULL) {
        return 0;
    }
    return font->height;
}

bool xt_eink_font_reload(const char *path)
{
    if (path == NULL) {
        return false;
    }

    // 关闭旧字体
    if (s_font != NULL) {
        xt_eink_font_close(s_font);
        s_font = NULL;
    }

    // 打开新字体
    s_font = xt_eink_font_open(path);
    if (s_font == NULL) {
        ESP_LOGE(TAG, "Failed to reload font: %s", path);
        return false;
    }

    // 更新保存的路径
    strncpy(s_loaded_font_path, path, sizeof(s_loaded_font_path) - 1);
    s_loaded_font_path[sizeof(s_loaded_font_path) - 1] = '\0';

    // 按需求：默认/菜单字体不缓存；用户字体使用 LittleFS 缓存，且仅保留当前一份。
    if (s_menu_font_path[0] != '\0' && strcmp(path, s_menu_font_path) == 0) {
        // 菜单字体：不启用缓存
    } else {
        if (!font_cache_init(path)) {
            ESP_LOGW(TAG, "Font cache init failed after reload, will use direct SD access");
        }
    }

    ESP_LOGI(TAG, "Font reloaded: %s", path);
    return true;
}

void xt_eink_font_deinit(void)
{
    // 释放用户字体（如果与菜单字体不同）
    if (s_font != NULL && s_font != s_menu_default_font) {
        ESP_LOGI(TAG, "Closing user font: %s", s_loaded_font_path);
        xt_eink_font_close(s_font);
        s_font = NULL;
    } else if (s_font != NULL) {
        // 如果用户字体等于菜单字体，只清除引用不关闭
        s_font = NULL;
    }
    
    // 释放菜单默认字体
    if (s_menu_default_font != NULL) {
        ESP_LOGI(TAG, "Closing menu default font: %s", s_menu_font_path);
        xt_eink_font_close(s_menu_default_font);
        s_menu_default_font = NULL;
    }
    
    // 清除路径缓存
    memset(s_loaded_font_path, 0, sizeof(s_loaded_font_path));
    memset(s_menu_font_path, 0, sizeof(s_menu_font_path));
    
    ESP_LOGI(TAG, "Font system deinitialized");
}
