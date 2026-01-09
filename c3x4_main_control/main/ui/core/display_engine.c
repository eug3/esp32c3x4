/**
 * @file display_engine.c
 * @brief 手绘 UI 显示引擎实现
 */

#include "display_engine.h"
#include "EPD_4in26.h"
#include "GUI_Paint.h"
#include "Fonts/fonts.h"
#include "xt_eink_font_impl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "DISP_ENGINE";

// 帧缓冲（1bpp，物理尺寸800x480 = 48KB）
// 注意：逻辑尺寸是480x800，通过ROTATE_270旋转显示
// 优化：使用动态分配节省 DRAM (.bss) 空间
#define FRAMEBUFFER_SIZE ((800 * 480) / 8)
static uint8_t *s_framebuffer = NULL;

// 显示引擎状态
static display_config_t s_config = {0};
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static dirty_region_t s_dirty_region = {0};
static int s_partial_refresh_count = 0;  // 局刷计数器

// 帧缓冲管理
#define MAX_PARTIAL_REFRESH_COUNT 1  // 最多连续 5 次局刷，然后强制全刷

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lock_engine(void);
static void unlock_engine(void);
static void expand_dirty_region(int x, int y, int width, int height);
static void clear_dirty_internal(void);  // 内部版本，调用者必须持有锁
static void convert_logical_to_physical_region(int lx, int ly, int lw, int lh,
                                                int *px, int *py, int *pw, int *ph);

static bool text_has_non_ascii(const char *text);
static int measure_text_width_utf8(const char *text, sFONT *ascii_font);
static int measure_text_height_utf8(const char *text, sFONT *ascii_font);
static int draw_text_utf8_locked(int x, int y, const char *text, sFONT *ascii_font, uint8_t color, uint8_t bg_color);

// 菜单专用：中文固定使用菜单默认字体（不受用户字体切换影响）
static int measure_text_width_utf8_menu(const char *text, sFONT *ascii_font);
static int measure_text_height_utf8_menu(const char *text, sFONT *ascii_font);
static int draw_text_utf8_menu_locked(int x, int y, const char *text, sFONT *ascii_font, uint8_t color, uint8_t bg_color);

static void ensure_xt_font_initialized(void);
static int get_cjk_typical_width(void);
static int get_ascii_advance_width(sFONT *ascii_font);
static sFONT* choose_ascii_font_by_cjk_height(void);
static sFONT* choose_ascii_font_by_cjk_height_menu(void);
static sFONT* choose_ascii_font_by_target_height(int target_height);

// 电量显示相关
static void draw_battery_to_framebuffer(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static void lock_engine(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }

    // 防御：确保所有 GUI_Paint 绘制都落在 display_engine 的 framebuffer 上。
    // 某些屏幕会临时 Paint_NewImage/SelectImage 到小 buffer，如果没有恢复，
    // 会导致“绘制有走，但刷新仍白屏”。
    if (s_framebuffer != NULL && Paint.Image != s_framebuffer) {
        Paint_SelectImage(s_framebuffer);
    }
}

static void unlock_engine(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void expand_dirty_region(int x, int y, int width, int height)
{
    if (!s_dirty_region.valid) {
        s_dirty_region.x = x;
        s_dirty_region.y = y;
        s_dirty_region.width = width;
        s_dirty_region.height = height;
        s_dirty_region.valid = true;
    } else {
        // 扩展区域以包含新区域
        int x1 = s_dirty_region.x;
        int y1 = s_dirty_region.y;
        int x2 = x1 + s_dirty_region.width - 1;
        int y2 = y1 + s_dirty_region.height - 1;

        int nx1 = x;
        int ny1 = y;
        int nx2 = x + width - 1;
        int ny2 = y + height - 1;

        s_dirty_region.x = (x1 < nx1) ? x1 : nx1;
        s_dirty_region.y = (y1 < ny1) ? y1 : ny1;
        s_dirty_region.width = ((x2 > nx2) ? x2 : nx2) - s_dirty_region.x + 1;
        s_dirty_region.height = ((y2 > ny2) ? y2 : ny2) - s_dirty_region.y + 1;
    }
}

static bool text_has_non_ascii(const char *text)
{
    if (text == NULL) {
        return false;
    }

    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        if ((*p & 0x80) != 0) {
            return true;
        }
        p++;
    }
    return false;
}

static void ensure_xt_font_initialized(void)
{
    static bool xt_font_initialized = false;
    if (!xt_font_initialized) {
        if (xt_eink_font_init()) {
            xt_font_initialized = true;
        }
    }
}

static sFONT* choose_ascii_font_by_target_height(int target_height)
{
    // 目标：混排时 ASCII 不要显得过小。
    // 策略：优先选择“高度 >= 中文行高”的最小字号（ceiling）。
    // 若目标高度超过现有 ASCII 字体最大高度，则退化为选择最大字号。
    // 候选 ASCII 字体按高度递增。
    sFONT *candidates[] = {
        &Font8,
        &Font12,
        &Font16,
        &SourceSansPro16,
        &Font20,
        &Font24,
    };

    if (target_height <= 0) {
        return &Font16;
    }

    // 先找 ceiling：第一个高度 >= target
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        sFONT *f = candidates[i];
        if ((int)f->Height >= target_height) {
            return f;
        }
    }

    // 找不到 ceiling：返回最大字号
    return candidates[(sizeof(candidates) / sizeof(candidates[0])) - 1];
}

static sFONT* choose_ascii_font_by_cjk_height(void)
{
    ensure_xt_font_initialized();
    int cjk_h = xt_eink_font_get_height();
    return choose_ascii_font_by_target_height(cjk_h);
}

static sFONT* choose_ascii_font_by_cjk_height_menu(void)
{
    ensure_xt_font_initialized();
    int cjk_h = xt_eink_font_menu_get_height();
    return choose_ascii_font_by_target_height(cjk_h);
}

sFONT* display_get_default_ascii_font(void)
{
    return choose_ascii_font_by_cjk_height();
}

sFONT* display_get_menu_font(void)
{
    // 菜单界面必须与“菜单中文高度”匹配，但不能受用户字体影响。
    // 这里按菜单中文字体高度自动挑选合适的 ASCII 字号。
    return choose_ascii_font_by_cjk_height_menu();
}

static int get_cjk_typical_width(void)
{
    // 使用常见汉字探测“全角宽度”，用于推导英文半角宽度
    static const uint32_t probes[] = {
        0x4E2Du, // 中
        0x56FDu, // 国
        0x6C49u, // 汉
        0x6587u, // 文
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        uint32_t ch = probes[i];
        if (!xt_eink_font_has_char(ch)) {
            continue;
        }
        xt_eink_glyph_t glyph;
        if (xt_eink_font_get_glyph(ch, &glyph) && glyph.width > 0) {
            return glyph.width;
        }
    }

    int h = xt_eink_font_get_height();
    if (h > 0) {
        // 字体文件通常接近方形，但本项目常用 19x25：宽度约为高度 * 0.76
        // 这里取 3/4 作为稳健近似。
        return (h * 3) / 4;
    }
    return 0;
}

static int get_ascii_advance_width(sFONT *ascii_font)
{
    if (ascii_font == NULL) {
        ascii_font = choose_ascii_font_by_cjk_height();
    }

    int cjk_w = get_cjk_typical_width();
    if (cjk_w <= 0) {
        return (int)ascii_font->Width;
    }

    // 半角宽度：约等于全角宽度的一半
    int target = (cjk_w + 1) / 2;
    int base = (int)ascii_font->Width;
    if (target <= base) {
        return base;
    }

    // 通过增加字距来“选择合适的英文宽度”，不做像素级拉伸
    int extra = target - base;
    if (extra > base) {
        extra = base; // 防止极端字体导致过大间距
    }
    return base + extra;
}

static int measure_text_width_utf8(const char *text, sFONT *ascii_font)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (ascii_font == NULL) {
        ascii_font = choose_ascii_font_by_cjk_height();
    }

    ensure_xt_font_initialized();
    int ascii_adv = get_ascii_advance_width(ascii_font);
    const int xt_h = xt_eink_font_get_height();
    const bool use_xt_ascii = (xt_h > 0 && ascii_font != NULL && (int)ascii_font->Height < xt_h);

    int width = 0;
    const char *p = text;
    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        // 规则：ASCII 永远走内置字体；非 ASCII 才尝试字体文件（xt_eink）
        if (ch <= 0x7Fu) {
            if (use_xt_ascii && xt_eink_font_has_char(ch)) {
                xt_eink_glyph_t glyph;
                if (xt_eink_font_get_glyph(ch, &glyph) && glyph.width > 0) {
                    width += glyph.width;
                } else {
                    width += ascii_adv;
                }
            } else {
                width += ascii_adv;
            }
        } else if (xt_eink_font_has_char(ch)) {
            xt_eink_glyph_t glyph;
            if (xt_eink_font_get_glyph(ch, &glyph) && glyph.width > 0) {
                width += glyph.width;
            } else {
                width += xt_eink_font_get_height();
            }
        } else {
            // 非 ASCII 且字体文件没有：用 '?' 回退
            width += ascii_adv;
        }

        p += offset;
    }

    return width;
}

static int measure_text_height_utf8(const char *text, sFONT *ascii_font)
{
    (void)text;

    if (ascii_font == NULL) {
        ascii_font = choose_ascii_font_by_cjk_height();
    }

    ensure_xt_font_initialized();

    int h = ascii_font->Height;
    int xt_h = xt_eink_font_get_height();
    if (xt_h > h) {
        h = xt_h;
    }
    return h;
}

static int measure_text_width_utf8_menu(const char *text, sFONT *ascii_font)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (ascii_font == NULL) {
        ascii_font = display_get_menu_font();
    }

    ensure_xt_font_initialized();
    const int ascii_adv = (int)ascii_font->Width;

    int width = 0;
    const char *p = text;
    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        if (ch <= 0x7Fu) {
            width += ascii_adv;
        } else if (xt_eink_font_menu_has_char(ch)) {
            xt_eink_glyph_t glyph;
            if (xt_eink_font_menu_get_glyph(ch, &glyph) && glyph.width > 0) {
                width += glyph.width;
            } else {
                width += xt_eink_font_menu_get_height();
            }
        } else {
            width += ascii_adv;
        }

        p += offset;
    }

    return width;
}

static int measure_text_height_utf8_menu(const char *text, sFONT *ascii_font)
{
    (void)text;

    if (ascii_font == NULL) {
        ascii_font = display_get_menu_font();
    }

    ensure_xt_font_initialized();

    int h = ascii_font->Height;
    int xt_h = xt_eink_font_menu_get_height();
    if (xt_h > h) {
        h = xt_h;
    }
    return h;
}

static int draw_text_utf8_menu_locked(int x, int y, const char *text, sFONT *ascii_font, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (ascii_font == NULL) {
        ascii_font = display_get_menu_font();
    }

    ensure_xt_font_initialized();
    const int ascii_adv = (int)ascii_font->Width;

    int text_w = measure_text_width_utf8_menu(text, ascii_font);
    int text_h = measure_text_height_utf8_menu(text, ascii_font);

    if (bg_color != COLOR_WHITE) {
        Paint_DrawRectangle(x, y, x + text_w - 1, y + text_h - 1, bg_color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }

    int current_x = x;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        if (ch <= 0x7Fu) {
            Paint_DrawChar(current_x, y, (char)ch, ascii_font, color, bg_color);
            current_x += ascii_adv;
        } else if (xt_eink_font_menu_has_char(ch)) {
            xt_eink_glyph_t glyph;
            if (!xt_eink_font_menu_get_glyph(ch, &glyph) || glyph.bitmap == NULL) {
                current_x += xt_eink_font_menu_get_height();
                p += offset;
                continue;
            }

            int bytes_per_row = (glyph.width + 7) / 8;
            for (int row = 0; row < glyph.height; row++) {
                for (int col = 0; col < glyph.width; col++) {
                    int byte_idx = row * bytes_per_row + col / 8;
                    int bit_idx = 7 - (col % 8);
                    bool pixel_set = (glyph.bitmap[byte_idx] >> bit_idx) & 1;
                    if (!pixel_set) {
                        continue;
                    }

                    int px = current_x + col;
                    int py = y + row;
                    if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                        Paint_SetPixel(px, py, color);
                    }
                }
            }

            current_x += glyph.width;
        } else {
            Paint_DrawChar(current_x, y, '?', ascii_font, color, bg_color);
            current_x += ascii_adv;
        }

        p += offset;
    }

    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, text_w, text_h);
    }

    return text_w;
}

static int draw_text_utf8_locked(int x, int y, const char *text, sFONT *ascii_font, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (ascii_font == NULL) {
        ascii_font = choose_ascii_font_by_cjk_height();
    }

    ensure_xt_font_initialized();
    const int xt_h = xt_eink_font_get_height();
    const bool use_xt_ascii = (xt_h > 0 && ascii_font != NULL && (int)ascii_font->Height < xt_h);
    int ascii_adv = get_ascii_advance_width(ascii_font);

    int text_w = measure_text_width_utf8(text, ascii_font);
    int text_h = measure_text_height_utf8(text, ascii_font);

    if (bg_color != COLOR_WHITE) {
        Paint_DrawRectangle(x, y, x + text_w - 1, y + text_h - 1, bg_color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }

    int current_x = x;
    const char *p = text;

    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        // 规则：ASCII 优先使用与中文等高的 xt 字体（若可用），否则回退内置字体；非 ASCII 走 xt 字体
        if (ch <= 0x7Fu) {
            bool drawn = false;
            if (use_xt_ascii && xt_eink_font_has_char(ch)) {
                xt_eink_glyph_t glyph;
                if (xt_eink_font_get_glyph(ch, &glyph) && glyph.bitmap != NULL) {
                    int bytes_per_row = (glyph.width + 7) / 8;
                    for (int row = 0; row < glyph.height; row++) {
                        for (int col = 0; col < glyph.width; col++) {
                            int byte_idx = row * bytes_per_row + col / 8;
                            int bit_idx = 7 - (col % 8);
                            bool pixel_set = (glyph.bitmap[byte_idx] >> bit_idx) & 1;
                            if (!pixel_set) {
                                continue;
                            }

                            int px = current_x + col;
                            int py = y + row;
                            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                                Paint_SetPixel(px, py, color);
                            }
                        }
                    }
                    current_x += glyph.width;
                    drawn = true;
                }
            }

            if (!drawn) {
                Paint_DrawChar(current_x, y, (char)ch, ascii_font, color, bg_color);
                current_x += ascii_adv;
            }
        } else if (xt_eink_font_has_char(ch)) {
            xt_eink_glyph_t glyph;
            if (!xt_eink_font_get_glyph(ch, &glyph) || glyph.bitmap == NULL) {
                current_x += xt_eink_font_get_height();
                p += offset;
                continue;
            }

            int bytes_per_row = (glyph.width + 7) / 8;
            for (int row = 0; row < glyph.height; row++) {
                for (int col = 0; col < glyph.width; col++) {
                    int byte_idx = row * bytes_per_row + col / 8;
                    int bit_idx = 7 - (col % 8);
                    bool pixel_set = (glyph.bitmap[byte_idx] >> bit_idx) & 1;
                    if (!pixel_set) {
                        continue;
                    }

                    int px = current_x + col;
                    int py = y + row;
                    if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                        Paint_SetPixel(px, py, color);
                    }
                }
            }

            current_x += glyph.width;
        } else {
            Paint_DrawChar(current_x, y, '?', ascii_font, color, bg_color);
            current_x += ascii_adv;
        }

        p += offset;
    }

    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, text_w, text_h);
    }

    return text_w;
}

/**
 * @brief 将逻辑坐标(ROTATE_270)转换为物理坐标
 * 
 * ROTATE_270转换规则（来自GUI_Paint.c）：
 *   物理X = 逻辑Y
 *   物理Y = 物理高度 - 逻辑X - 1
 * 
 * 对于区域转换：
 *   逻辑区域：(lx, ly, lw, lh) - 480x800坐标系
 *   物理区域：(px, py, pw, ph) - 800x480坐标系
 * 
 * @param lx 逻辑起始X（0~479）
 * @param ly 逻辑起始Y（0~799）
 * @param lw 逻辑宽度
 * @param lh 逻辑高度
 * @param px 输出：物理起始X
 * @param py 输出：物理起始Y
 * @param pw 输出：物理宽度
 * @param ph 输出：物理高度
 */
static void convert_logical_to_physical_region(int lx, int ly, int lw, int lh,
                                                int *px, int *py, int *pw, int *ph)
{
    // 物理尺寸（帧缓冲实际尺寸）
    const int PHYSICAL_HEIGHT = 480;
    
    // 逻辑区域的四个角点
    int lx1 = lx;
    int ly1 = ly;
    int lx2 = lx + lw - 1;
    int ly2 = ly + lh - 1;
    
    // 转换四个角点到物理坐标
    // 左上角 (lx1, ly1) -> (ly1, PHYSICAL_HEIGHT - lx1 - 1)
    int p_x1 = ly1;
    int p_y1 = PHYSICAL_HEIGHT - lx1 - 1;
    
    // 右下角 (lx2, ly2) -> (ly2, PHYSICAL_HEIGHT - lx2 - 1)
    int p_x2 = ly2;
    int p_y2 = PHYSICAL_HEIGHT - lx2 - 1;
    
    // 注意：由于旋转，物理Y坐标是反向的（lx越大，p_y越小）
    // 所以需要交换p_y1和p_y2
    int temp = p_y1;
    p_y1 = p_y2;
    p_y2 = temp;
    
    // 计算物理区域的起始点和尺寸
    *px = p_x1;
    *py = p_y1;
    *pw = p_x2 - p_x1 + 1;
    *ph = p_y2 - p_y1 + 1;
    
    ESP_LOGD(TAG, "Coord convert: logical(%d,%d,%d,%d) -> physical(%d,%d,%d,%d)",
             lx, ly, lw, lh, *px, *py, *pw, *ph);
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool display_engine_init(const display_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Display engine already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing display engine...");

    // 分配帧缓冲区内存（动态分配，节省 47KB .bss 空间）
    if (s_framebuffer == NULL) {
        ESP_LOGI(TAG, "Allocating framebuffer: %d bytes", FRAMEBUFFER_SIZE);
        s_framebuffer = heap_caps_malloc(FRAMEBUFFER_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (s_framebuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate framebuffer! Free heap: %lu bytes", 
                     esp_get_free_heap_size());
            return false;
        }
        memset(s_framebuffer, 0, FRAMEBUFFER_SIZE);
        ESP_LOGI(TAG, "Framebuffer allocated successfully. Free heap now: %lu bytes", 
                 esp_get_free_heap_size());
    }

    // 创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(s_framebuffer);
        s_framebuffer = NULL;
        return false;
    }

    // 应用配置
    if (config != NULL) {
        s_config = *config;
    } else {
        // 默认配置
        s_config.use_partial_refresh = true;
        s_config.auto_refresh = false;
        s_config.default_mode = REFRESH_MODE_PARTIAL;
    }

    // 初始化 GUI_Paint
    // 参数：缓冲区, 物理宽度, 物理高度, 旋转角度, 背景色
    // ROTATE_270: 逻辑竖屏(480x800) -> 物理横屏(800x480)
    Paint_NewImage(s_framebuffer, 800, 480, ROTATE_270, WHITE);
    Paint_SelectImage(s_framebuffer);
    Paint_SetScale(2);  // 1bpp 黑白模式（帧缓冲区只有48KB，不支持4bpp的192KB）
    Paint_Clear(WHITE);

    // 清除脏区域
    memset(&s_dirty_region, 0, sizeof(s_dirty_region));

    s_initialized = true;
    ESP_LOGI(TAG, "Display engine initialized");
    ESP_LOGI(TAG, "  Framebuffer: %d bytes", FRAMEBUFFER_SIZE);
    ESP_LOGI(TAG, "  Partial refresh: %s", s_config.use_partial_refresh ? "Yes" : "No");
    ESP_LOGI(TAG, "  Auto refresh: %s", s_config.auto_refresh ? "Yes" : "No");

    return true;
}

void display_engine_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    lock_engine();

    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    // 释放帧缓冲区内存
    if (s_framebuffer != NULL) {
        ESP_LOGI(TAG, "Freeing framebuffer");
        free(s_framebuffer);
        s_framebuffer = NULL;
    }

    s_initialized = false;
    unlock_engine();

    ESP_LOGI(TAG, "Display engine deinitialized");
}

void display_set_battery_callback(display_battery_read_t read_battery)
{
    lock_engine();
    s_config.read_battery_pct = read_battery;
    unlock_engine();
    ESP_LOGI(TAG, "Battery callback %s", read_battery ? "set" : "cleared");
}

void display_clear(uint8_t color)
{
    ESP_LOGI(TAG, "display_clear START: color=0x%02X", color);
    ESP_LOGI(TAG, "Locking...");
    lock_engine();
    ESP_LOGI(TAG, "Locked. Paint.Scale=%d, WidthByte=%d, HeightByte=%d", 
             Paint.Scale, Paint.WidthByte, Paint.HeightByte);
    
    ESP_LOGI(TAG, "Calling Paint_Clear...");
    Paint_Clear(color);
    ESP_LOGI(TAG, "Paint_Clear done");
    
    if (s_config.use_partial_refresh) {
        expand_dirty_region(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    ESP_LOGI(TAG, "Unlocking...");
    unlock_engine();
    ESP_LOGI(TAG, "display_clear END");

    if (s_config.auto_refresh) {
        display_refresh(s_config.default_mode);
    }
}

void display_clear_region(int x, int y, int width, int height, uint8_t color)
{
    lock_engine();
    Paint_ClearWindows(x, y, x + width - 1, y + height - 1, color);
    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, width, height);
    }
    unlock_engine();

    if (s_config.auto_refresh) {
        display_refresh_region(x, y, width, height, s_config.default_mode);
    }
}

void display_refresh(refresh_mode_t mode)
{
    lock_engine();

    // 统计刷新前（不含电量叠加）的帧缓存变化量
    size_t non_white_before_battery = 0;
    if (s_framebuffer != NULL) {
        for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
            if (s_framebuffer[i] != 0xFFu) {
                non_white_before_battery++;
            }
        }
    }

    // 在刷新前绘制电量到帧缓存（叠加）
    draw_battery_to_framebuffer();

    ESP_LOGI(TAG, "Refreshing display (mode=%d)...", mode);

    // 统计帧缓冲变化量（白屏诊断）：white=0xFF，任何绘制都会让某些字节变为非 0xFF。
    size_t non_white = 0;
    if (s_framebuffer != NULL) {
        for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
            if (s_framebuffer[i] != 0xFFu) {
                non_white++;
            }
        }
    }
    ESP_LOGI(TAG, "Framebuffer non-white bytes (before battery): %u / %u", (unsigned)non_white_before_battery, (unsigned)FRAMEBUFFER_SIZE);
    ESP_LOGI(TAG, "Framebuffer non-white bytes (after  battery): %u / %u (delta=%d)",
             (unsigned)non_white, (unsigned)FRAMEBUFFER_SIZE,
             (int)non_white - (int)non_white_before_battery);

    // 打印帧缓冲区前几个字节用于调试
    ESP_LOGI(TAG, "Framebuffer first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
             s_framebuffer[0], s_framebuffer[1], s_framebuffer[2], s_framebuffer[3],
             s_framebuffer[4], s_framebuffer[5], s_framebuffer[6], s_framebuffer[7]);

    // 根据刷新模式选择 EPD 函数
    switch (mode) {
        case REFRESH_MODE_FULL:
            EPD_4in26_Display(s_framebuffer);
            s_partial_refresh_count = 0;  // 全刷重置计数器
            ESP_LOGI(TAG, "Full refresh, reset partial count to 0");
            break;
        case REFRESH_MODE_PARTIAL:
        default:
            // 最简单且一致的策略：局刷只刷新"当前脏区"(逻辑坐标 480x800)
            // 避免全屏局刷导致"只画了局部却把整屏刷成空白/白屏"的观感。
            if (!s_dirty_region.valid) {
                ESP_LOGI(TAG, "No dirty region; skip partial refresh");
                break;
            }

            // 边界检查（逻辑坐标）
            int x = s_dirty_region.x;
            int y = s_dirty_region.y;
            int width = s_dirty_region.width;
            int height = s_dirty_region.height;
            ESP_LOGD(TAG, "dirty_region raw: x=%d y=%d w=%d h=%d", x, y, width, height);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
            if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

            // 逻辑 -> 物理（ROTATE_270）
            int phys_x, phys_y, phys_w, phys_h;
            convert_logical_to_physical_region(x, y, width, height, &phys_x, &phys_y, &phys_w, &phys_h);

            // 调试：打印脏区前几字节
            int phys_x_aligned = phys_x - (phys_x % 8);
            int phys_x_bytes = phys_x_aligned / 8;
            //int phys_w_bytes = (phys_w + (phys_x % 8) + 7) / 8;

            ESP_LOGI(TAG, "Dirty region data (first 4 rows):");
            for (int dbg_row = 0; dbg_row < 4 && dbg_row < phys_h; dbg_row++) {
                UBYTE *row_ptr = s_framebuffer + (phys_y + dbg_row) * 100 + phys_x_bytes;
                ESP_LOGI(TAG, "  Row %d: %02X %02X %02X %02X",
                         dbg_row, row_ptr[0], row_ptr[1], row_ptr[2], row_ptr[3]);
            }

            // 标准局刷模式：只写 0x24，依赖 0x26 中的旧数据作为对比基准
            //EPD_4in26_Display_Part_Stream(s_framebuffer, 100, phys_x, phys_y, phys_w, phys_h);
            EPD_4in26_Display_Fast(s_framebuffer);
            break;
    }

    clear_dirty_internal();  // 使用内部版本，避免嵌套锁
    unlock_engine();

    ESP_LOGI(TAG, "display_refresh complete");
}

void display_debug_log_framebuffer(const char *tag)
{
    lock_engine();

    size_t non_white = 0;
    if (s_framebuffer != NULL) {
        for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
            if (s_framebuffer[i] != 0xFFu) {
                non_white++;
            }
        }
    }

    ESP_LOGI(TAG, "FB[%s]: s_framebuffer=%p Paint.Image=%p non_white=%u/%u first8=%02X %02X %02X %02X %02X %02X %02X %02X",
             (tag != NULL) ? tag : "(null)",
             (void *)s_framebuffer,
             (void *)Paint.Image,
             (unsigned)non_white,
             (unsigned)FRAMEBUFFER_SIZE,
             s_framebuffer ? s_framebuffer[0] : 0,
             s_framebuffer ? s_framebuffer[1] : 0,
             s_framebuffer ? s_framebuffer[2] : 0,
             s_framebuffer ? s_framebuffer[3] : 0,
             s_framebuffer ? s_framebuffer[4] : 0,
             s_framebuffer ? s_framebuffer[5] : 0,
             s_framebuffer ? s_framebuffer[6] : 0,
             s_framebuffer ? s_framebuffer[7] : 0);

    unlock_engine();
}

void display_refresh_region(int x, int y, int width, int height, refresh_mode_t mode)
{
    // 边界检查（逻辑坐标）
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

    lock_engine();

    // 在刷新前绘制电量到帧缓存
    draw_battery_to_framebuffer();

    ESP_LOGI(TAG, "Refreshing region (logical): x=%d, y=%d, w=%d, h=%d (mode=%d)",
             x, y, width, height, mode);

    // 只有部分刷新支持区域刷新
    if (mode == REFRESH_MODE_PARTIAL) {
        // 将逻辑坐标转换为物理坐标（ROTATE_270）
        int phys_x, phys_y, phys_w, phys_h;
        convert_logical_to_physical_region(x, y, width, height,
                                          &phys_x, &phys_y, &phys_w, &phys_h);

        ESP_LOGI(TAG, "Physical region: x=%d, y=%d, w=%d, h=%d",
                 phys_x, phys_y, phys_w, phys_h);

        // 标准局刷模式：只写 0x24，依赖 0x26 中的旧数据作为对比基准
        EPD_4in26_Display_Part_Stream(s_framebuffer, 100, phys_x, phys_y, phys_w, phys_h);
    } else {
        // 全刷不支持区域，使用全屏刷新
        EPD_4in26_Display(s_framebuffer);
    }

    clear_dirty_internal();  // 使用内部版本，避免嵌套锁
    unlock_engine();
}

void display_mark_dirty(int x, int y, int width, int height)
{
    if (!s_config.use_partial_refresh) {
        return;
    }

    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

    ESP_LOGD(TAG, "mark_dirty: x=%d y=%d w=%d h=%d", x, y, width, height);

    lock_engine();
    expand_dirty_region(x, y, width, height);
    unlock_engine();
}

const dirty_region_t* display_get_dirty_region(void)
{
    return &s_dirty_region;
}

// 内部函数：清除脏区域（调用者必须已持有锁）
static void clear_dirty_internal(void)
{
    s_dirty_region.valid = false;
}

void display_clear_dirty(void)
{
    lock_engine();
    clear_dirty_internal();
    unlock_engine();
}

void display_draw_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    lock_engine();
    // 在 1bpp (Scale=2) 模式下，需要将灰度值 (0-255) 转换为黑白 (0 或 1)
    // 使用阈值 128 作为黑白分界点
    uint8_t bw_color = (color < 128) ? BLACK : WHITE;
    Paint_SetPixel(x, y, bw_color);
    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, 1, 1);
    }
    unlock_engine();
}

void display_draw_rect(int x, int y, int width, int height, uint8_t color, bool fill)
{
    lock_engine();

    if (fill) {
        // 填充矩形
        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++) {
                int px = x + i;
                int py = y + j;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    Paint_SetPixel(px, py, color);
                }
            }
        }
    } else {
        // 空心矩形 - 直接绘制避免嵌套锁
        for (int i = 0; i < width; i++) {
            int px = x + i;
            if (px >= 0 && px < SCREEN_WIDTH) {
                if (y >= 0 && y < SCREEN_HEIGHT) Paint_SetPixel(px, y, color);
                if (y + height - 1 >= 0 && y + height - 1 < SCREEN_HEIGHT) Paint_SetPixel(px, y + height - 1, color);
            }
        }
        for (int j = 0; j < height; j++) {
            int py = y + j;
            if (py >= 0 && py < SCREEN_HEIGHT) {
                if (x >= 0 && x < SCREEN_WIDTH) Paint_SetPixel(x, py, color);
                if (x + width - 1 >= 0 && x + width - 1 < SCREEN_WIDTH) Paint_SetPixel(x + width - 1, py, color);
            }
        }
    }

    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, width, height);
    }
    unlock_engine();
}

int display_draw_text(int x, int y, const char *text, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    return display_draw_text_font(x, y, text, NULL, color, bg_color);
}

int display_draw_text_font(int x, int y, const char *text, sFONT *font, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (font == NULL) {
        font = choose_ascii_font_by_cjk_height();
    }

    lock_engine();
    int width = draw_text_utf8_locked(x, y, text, font, color, bg_color);
    unlock_engine();

    return width;
}

int display_draw_text_menu(int x, int y, const char *text, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    sFONT *font = display_get_menu_font();

    lock_engine();
    int width;
    if (text_has_non_ascii(text)) {
        width = draw_text_utf8_menu_locked(x, y, text, font, color, bg_color);
    } else {
        const int ascii_adv = (int)font->Width;
        int cur_x = x;
        for (const char *p = text; *p != '\0'; p++) {
            Paint_DrawChar(cur_x, y, *p, font, color, bg_color);
            cur_x += ascii_adv;
        }
        width = cur_x - x;

        if (s_config.use_partial_refresh) {
            expand_dirty_region(x, y, width, font->Height);
        }
    }
    unlock_engine();

    return width;
}

int display_get_text_width_font(const char *text, sFONT *font)
{
    if (text == NULL) {
        return 0;
    }

    if (font == NULL) {
        font = choose_ascii_font_by_cjk_height();
    }

    if (text_has_non_ascii(text)) {
        return measure_text_width_utf8(text, font);
    }

    return (int)strlen(text) * get_ascii_advance_width(font);
}

int display_get_text_width_menu(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    sFONT *font = display_get_menu_font();
    if (text_has_non_ascii(text)) {
        return measure_text_width_utf8_menu(text, font);
    }

    // 修复: 对包含中文的文本，使用 UTF-8 宽度计算，不能使用字节数
    return measure_text_width_utf8_menu(text, font);
}

int display_get_text_height_font(sFONT *font)
{
    if (font == NULL) {
        font = choose_ascii_font_by_cjk_height();
    }

    int h = font->Height;
    int xt_h = xt_eink_font_get_height();
    if (xt_h > h) {
        h = xt_h;
    }
    return h;
}

int display_get_text_height_menu(void)
{
    sFONT *font = display_get_menu_font();

    int h = font->Height;
    int xt_h = xt_eink_font_menu_get_height();
    if (xt_h > h) {
        h = xt_h;
    }
    return h;
}

uint8_t* display_get_framebuffer(void)
{
    return s_framebuffer;
}

void display_draw_bitmap_mask_1bpp(int x, int y, int width, int height,
                                  const uint8_t *bits, int stride_bytes,
                                  uint8_t color)
{
    if (bits == NULL || width <= 0 || height <= 0 || stride_bytes <= 0) {
        return;
    }

    lock_engine();

    for (int row = 0; row < height; row++) {
        const uint8_t *row_bits = bits + (row * stride_bytes);
        int py = y + row;
        if (py < 0 || py >= SCREEN_HEIGHT) {
            continue;
        }

        for (int col = 0; col < width; col++) {
            int px = x + col;
            if (px < 0 || px >= SCREEN_WIDTH) {
                continue;
            }

            uint8_t b = row_bits[col / 8];
            uint8_t mask = (uint8_t)(0x80u >> (col % 8));
            if ((b & mask) != 0) {
                Paint_SetPixel((UWORD)px, (UWORD)py, (UWORD)color);
            }
        }
    }

    if (s_config.use_partial_refresh) {
        expand_dirty_region(x, y, width, height);
    }

    unlock_engine();

    if (s_config.auto_refresh) {
        display_refresh(s_config.default_mode);
    }
}

// 在帧缓存右上角绘制电量信息 "Battery: xxx"
static void draw_battery_to_framebuffer(void)
{
    // 检查是否有电量读取回调
    if (s_config.read_battery_pct == NULL) {
        return;
    }

    // 读取电量值
    uint8_t battery_pct = s_config.read_battery_pct();

    // 格式化字符串 "Battery: xxx"（三位数字，不用百分号）
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "Battery: %03d", battery_pct);

    // 使用菜单字体在右上角绘制
    sFONT *font = display_get_menu_font();
    int text_width = display_get_text_width_menu(bat_str);

    // 计算位置（右上角）
    int x = SCREEN_WIDTH - text_width - 10;
    int y = 5;  // 顶部留一点边距

    // 绘制文字到帧缓存
    const int ascii_adv = (int)font->Width;
    for (const char *p = bat_str; *p != '\0'; p++) {
        Paint_DrawChar(x, y, *p, font, COLOR_BLACK, COLOR_WHITE);
        x += ascii_adv;
    }
}
