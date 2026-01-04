/**
 * @file display_engine.c
 * @brief 手绘 UI 显示引擎实现
 */

#include "display_engine.h"
#include "EPD_4in26.h"
#include "GUI_Paint.h"
#include "Fonts/fonts.h"
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
    
    display_mark_dirty(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    display_mark_dirty(x, y, width, height);
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
        case REFRESH_MODE_FAST:
            EPD_4in26_Display_Fast(s_framebuffer);
            break;
        case REFRESH_MODE_PARTIAL:
        default:
            EPD_4in26_Display_Part(s_framebuffer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
    }

    display_clear_dirty();
    unlock_engine();
}

void display_refresh_region(int x, int y, int width, int height, refresh_mode_t mode)
{
    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

    lock_engine();

    ESP_LOGI(TAG, "Refreshing region: x=%d, y=%d, w=%d, h=%d (mode=%d)",
             x, y, width, height, mode);

    // 只有部分刷新支持区域刷新
    if (mode == REFRESH_MODE_PARTIAL) {
        EPD_4in26_Display_Part(s_framebuffer, x, y, width, height);
    } else {
        // 全刷和快刷不支持区域，使用全屏刷新
        if (mode == REFRESH_MODE_FULL) {
            EPD_4in26_Display(s_framebuffer);
        } else {
            EPD_4in26_Display_Fast(s_framebuffer);
        }
    }

    display_clear_dirty();
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

    lock_engine();
    expand_dirty_region(x, y, width, height);
    unlock_engine();
}

const dirty_region_t* display_get_dirty_region(void)
{
    return &s_dirty_region;
}

void display_clear_dirty(void)
{
    lock_engine();
    s_dirty_region.valid = false;
    unlock_engine();
}

void display_draw_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    lock_engine();
    Paint_SetPixel(x, y, color);
    display_mark_dirty(x, y, 1, 1);
    unlock_engine();
}

void display_draw_hline(int x, int y, int width, uint8_t color)
{
    if (y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (x + width > SCREEN_WIDTH) {
        width = SCREEN_WIDTH - x;
    }

    lock_engine();
    for (int i = 0; i < width; i++) {
        Paint_SetPixel(x + i, y, color);
    }
    display_mark_dirty(x, y, width, 1);
    unlock_engine();
}

void display_draw_vline(int x, int y, int height, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH) {
        return;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (y + height > SCREEN_HEIGHT) {
        height = SCREEN_HEIGHT - y;
    }

    lock_engine();
    for (int i = 0; i < height; i++) {
        Paint_SetPixel(x, y + i, color);
    }
    display_mark_dirty(x, y, 1, height);
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
        // 空心矩形
        display_draw_hline(x, y, width, color);
        display_draw_hline(x, y + height - 1, width, color);
        display_draw_vline(x, y, height, color);
        display_draw_vline(x + width - 1, y, height, color);
    }

    display_mark_dirty(x, y, width, height);
    unlock_engine();
}

int display_draw_text(int x, int y, const char *text, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    lock_engine();
    // 使用 GUI_Paint 的默认字体（font12）
    Paint_DrawString_EN(x, y, text, &Font12, color, bg_color);

    // 计算文本宽度
    int width = strlen(text) * Font12.Width;

    display_mark_dirty(x, y, width, Font12.Height);
    unlock_engine();

    return width;
}

int display_draw_text_cn(int x, int y, const char *text, int font_size, uint8_t color, uint8_t bg_color)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    lock_engine();

    // TODO: 实现中文字体渲染
    // 暂时使用英文字体替代
    Paint_DrawString_EN(x, y, text, &Font12, color, bg_color);
    int width = strlen(text) * Font12.Width;

    display_mark_dirty(x, y, width, Font12.Height);
    unlock_engine();

    return width;
}

int display_get_text_width(const char *text, int font_size)
{
    if (text == NULL) {
        return 0;
    }

    // TODO: 根据字体大小计算
    return strlen(text) * Font12.Width;
}

int display_get_text_height(int font_size)
{
    // TODO: 根据字体大小返回
    return Font12.Height;
}

void display_draw_bitmap(int x, int y, int width, int height, const uint8_t *bitmap, bool invert)
{
    if (bitmap == NULL) {
        return;
    }

    lock_engine();

    // TODO: 实现位图绘制
    // GUI_Paint 的 Paint_DrawBitMap 需要特定格式

    display_mark_dirty(x, y, width, height);
    unlock_engine();
}

uint8_t* display_get_framebuffer(void)
{
    return s_framebuffer;
}

void display_wait_refresh_complete(void)
{
    // EPD_4in26_ReadBusy() 会被 EPD 函数内部调用
    // 这里可以添加额外的等待逻辑
    vTaskDelay(pdMS_TO_TICKS(10));
}
