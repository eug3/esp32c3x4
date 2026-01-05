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
static uint8_t s_framebuffer[(800 * 480) / 8] = {0};

// 显示引擎状态
static display_config_t s_config = {0};
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static dirty_region_t s_dirty_region = {0};

// 帧缓冲管理
#define FRAMEBUFFER_SIZE (sizeof(s_framebuffer))

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

static void ensure_xt_font_initialized(void);
static int get_cjk_typical_width(void);
static int get_ascii_advance_width(sFONT *ascii_font);
static sFONT* choose_ascii_font_by_cjk_height(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static void lock_engine(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
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
        xt_eink_font_init();
        xt_font_initialized = true;
    }
}

static sFONT* choose_ascii_font_by_cjk_height(void)
{
    ensure_xt_font_initialized();
    int cjk_h = xt_eink_font_get_height();
    if (cjk_h <= 0) {
        return &Font12;
    }

    // 候选 ASCII 字体：只按高度挑选“最接近”的。
    // SourceSansPro16 是 21px 高，能填补 Font20/Font24 之间的空档。
    sFONT *candidates[] = {
        &Font8,
        &Font12,
        &Font16,
        &SourceSansPro16,
        &Font20,
        &Font24,
    };

    sFONT *best = candidates[0];
    int best_diff = 0x7FFFFFFF;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        sFONT *f = candidates[i];
        int diff = (int)f->Height - cjk_h;
        if (diff < 0) diff = -diff;

        if (diff < best_diff) {
            best = f;
            best_diff = diff;
        } else if (diff == best_diff) {
            // 同差值时，优先选择更小的字体，避免英文“顶到行高”显得拥挤
            if (f->Height < best->Height) {
                best = f;
            }
        }
    }
    return best;
}

sFONT* display_get_default_ascii_font(void)
{
    return choose_ascii_font_by_cjk_height();
}

static int get_cjk_typical_width(void)
{
    // 使用常见汉字探测“全角宽度”，用于推导英文半角宽度
    ensure_xt_font_initialized();

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
            width += ascii_adv;
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

static int draw_text_utf8_locked(int x, int y, const char *text, sFONT *ascii_font, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (ascii_font == NULL) {
        ascii_font = choose_ascii_font_by_cjk_height();
    }

    ensure_xt_font_initialized();
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

        // 规则：ASCII 永远走内置字体；非 ASCII 才尝试字体文件（xt_eink）
        if (ch <= 0x7Fu) {
            Paint_DrawChar(current_x, y, (char)ch, ascii_font, color, bg_color);
            current_x += ascii_adv;
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

    // 创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
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

    s_initialized = false;
    unlock_engine();

    ESP_LOGI(TAG, "Display engine deinitialized");
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

    ESP_LOGI(TAG, "Refreshing display (mode=%d)...", mode);

    // 打印帧缓冲区前几个字节用于调试
    ESP_LOGI(TAG, "Framebuffer first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
             s_framebuffer[0], s_framebuffer[1], s_framebuffer[2], s_framebuffer[3],
             s_framebuffer[4], s_framebuffer[5], s_framebuffer[6], s_framebuffer[7]);

    // 根据刷新模式选择 EPD 函数
    switch (mode) {
        case REFRESH_MODE_FULL:
            EPD_4in26_Display(s_framebuffer);
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

            // 计算对齐后的字节宽度
            int phys_x_aligned = phys_x - (phys_x % 8);
            int phys_w_aligned = phys_w + (phys_x % 8);

            // 标准局刷模式：只写 0x24，依赖 0x26 中的旧数据作为对比基准
            EPD_4in26_Display_Part_Stream(s_framebuffer, 100, phys_x, phys_y, phys_w, phys_h);
            break;
    }

    clear_dirty_internal();  // 使用内部版本，避免嵌套锁
    unlock_engine();

    ESP_LOGI(TAG, "display_refresh complete");
}

void display_refresh_region(int x, int y, int width, int height, refresh_mode_t mode)
{
    // 边界检查（逻辑坐标）
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

    lock_engine();

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
    int width;
    if (text_has_non_ascii(text)) {
        width = draw_text_utf8_locked(x, y, text, font, color, bg_color);
    } else {
        // 与混排保持一致：按“半角宽度”逐字符绘制
        int ascii_adv = get_ascii_advance_width(font);
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

uint8_t* display_get_framebuffer(void)
{
    return s_framebuffer;
}
