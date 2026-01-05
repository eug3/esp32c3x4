/**
 * @file png_helper.c
 * @brief PNG 图片解码和显示辅助模块实现 - 使用 PNGdec 库
 */

#include "png_helper.h"
#include "display_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

// 包含 PNGdec (必须在 png_helper.h 之后)
extern "C" {
#include "PNGdec.h"
}

static const char *TAG = "PNG_HELPER";

// PNG 解码上下文
static struct {
    int dest_x;
    int dest_y;
    int dest_width;
    int dest_height;
    float x_scale;
    float y_scale;
    int last_y;
    bool success;
} png_context;

/**
 * @brief PNG 绘制回调函数
 */
static int png_draw_callback(PNGDRAW *pDraw)
{
    // RGB565 转灰度公式
    // RGB565: RRRRRGGG GGGBBBBB
    // 灰度 = (R*38 + G*75 + B*15) >> 7

    uint16_t *pPixels = (uint16_t *)pDraw->pPixels;

    for (int x = 0; x < pDraw->iWidth; x++) {
        uint16_t pixel = pPixels[x];

        // 从 RGB565 提取 RGB
        uint8_t r = ((pixel >> 11) & 0x1F) << 3;  // 5位 → 8位
        uint8_t g = ((pixel >> 5) & 0x3F) << 2;   // 6位 → 8位
        uint8_t b = (pixel & 0x1F) << 3;          // 5位 → 8位

        // 转换为灰度
        uint8_t gray = (r * 38 + g * 75 + b * 15) >> 7;

        // 计算目标坐标 (应用缩放)
        int dest_x = png_context.dest_x + (int)(x * png_context.x_scale);
        int dest_y = png_context.dest_y + (int)(pDraw->y * png_context.y_scale);

        // 绘制像素 (考虑缩放)
        if (png_context.x_scale >= 1.0f && png_context.y_scale >= 1.0f) {
            int scale_x = (int)png_context.x_scale;
            int scale_y = (int)png_context.y_scale;

            for (int sy = 0; sy < scale_y; sy++) {
                for (int sx = 0; sx < scale_x; sx++) {
                    display_draw_pixel(dest_x + sx, dest_y + sy, gray);
                }
            }
        } else {
            display_draw_pixel(dest_x, dest_y, gray);
        }
    }

    // 喂狗延迟 (每行)
    if (pDraw->y != png_context.last_y) {
        png_context.last_y = pDraw->y;
        vTaskDelay(1);
    }

    return 1;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool png_helper_get_size(const uint8_t *png_data, size_t png_data_size,
                         int *width, int *height)
{
    if (png_data == NULL || png_data_size < 8) {
        ESP_LOGE(TAG, "Invalid PNG data");
        return false;
    }

    // 检查 PNG 签名
    static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(png_data, png_sig, 8) != 0) {
        ESP_LOGE(TAG, "Not a PNG file");
        return false;
    }

    // 使用 PNGdec 获取图片尺寸
    PNG png;
    int rc = png.openRAM((uint8_t *)png_data, png_data_size, NULL);

    if (rc == 0) {  // PNG_SUCCESS = 0
        *width = png.getWidth();
        *height = png.getHeight();
        png.close();

        ESP_LOGI(TAG, "PNG size: %dx%d", *width, *height);
        return true;
    }

    ESP_LOGE(TAG, "Failed to decode PNG: %d", rc);
    return false;
}

bool png_helper_render(const uint8_t *png_data, size_t png_data_size,
                       int x, int y, int width, int height, bool clear_bg)
{
    if (png_data == NULL || png_data_size < 8) {
        ESP_LOGE(TAG, "Invalid PNG data");
        return false;
    }

    // 分配 PNG 解码器 (需要较大内存,优先使用 SPIRAM)
    PNG *png = (PNG *)heap_caps_malloc(sizeof(PNG), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (png == NULL) {
        png = (PNG *)heap_caps_malloc(sizeof(PNG), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }

    if (png == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PNG decoder");
        return false;
    }

    // 初始化上下文
    memset(&png_context, 0, sizeof(png_context));
    png_context.dest_x = x;
    png_context.dest_y = y;
    png_context.dest_width = width;
    png_context.dest_height = height;
    png_context.last_y = -1;
    png_context.success = false;

    // 打开 PNG
    int rc = png->openRAM((uint8_t *)png_data, png_data_size, png_draw_callback);

    if (rc != PNG_SUCCESS) {
        ESP_LOGE(TAG, "Failed to open PNG: %d", rc);
        heap_caps_free(png);
        return false;
    }

    // 获取图片尺寸
    int src_width = png->getWidth();
    int src_height = png->getHeight();

    ESP_LOGI(TAG, "PNG: %dx%d, bpp=%d", src_width, src_height, png->getBpp());

    // 计算缩放比例 (保持宽高比)
    float scale_w = (float)width / (float)src_width;
    float scale_h = (float)height / (float)src_height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    // 如果图片比目标区域小,不放大
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    png_context.x_scale = scale;
    png_context.y_scale = scale;

    // 计算实际显示尺寸
    int actual_width = (int)(src_width * scale);
    int actual_height = (int)(src_height * scale);

    // 计算居中偏移
    int offset_x = x + (width - actual_width) / 2;
    int offset_y = y + (height - actual_height) / 2;

    png_context.dest_x = offset_x;
    png_context.dest_y = offset_y;

    ESP_LOGI(TAG, "PNG render: scale=%.2f, offset=(%d,%d), size=(%d,%d)",
             scale, offset_x, offset_y, actual_width, actual_height);

    // 清除背景
    if (clear_bg) {
        display_clear_region(x, y, width, height, COLOR_WHITE);
    }

    // 解码并绘制
    rc = png->decode(&png_context, PNG_FAST_PALETTE);

    if (rc == PNG_SUCCESS) {
        png_context.success = true;
        ESP_LOGI(TAG, "PNG decoded successfully");
    } else {
        ESP_LOGE(TAG, "PNG decode failed: %d", rc);
    }

    // 关闭并释放
    png->close();
    heap_caps_free(png);

    return png_context.success;
}

bool png_helper_render_fullscreen(const uint8_t *png_data, size_t png_data_size)
{
    return png_helper_render(png_data, png_data_size,
                            0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, true);
}
