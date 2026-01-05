/**
 * @file bmp_helper.c
 * @brief BMP 图片解码和显示辅助模块实现
 */

#include "bmp_helper.h"
#include "display_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "BMP_HELPER";

// BMP 文件头结构
#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;      // 文件类型,必须是 "BM"
    uint32_t bfSize;      // 文件大小
    uint16_t bfReserved1; // 保留
    uint16_t bfReserved2; // 保留
    uint32_t bfOffBits;   // 从文件头到像素数据的偏移
} BMPFileHeader;

typedef struct {
    uint32_t biSize;          // 信息头大小
    int32_t  biWidth;         // 图像宽度
    int32_t  biHeight;        // 图像高度
    uint16_t biPlanes;        // 位平面数
    uint16_t biBitCount;      // 每像素位数
    uint32_t biCompression;   // 压缩类型
    uint32_t biSizeImage;     // 图像大小
    int32_t  biXPelsPerMeter; // 水平分辨率
    int32_t  biYPelsPerMeter; // 垂直分辨率
    uint32_t biClrUsed;       // 使用的颜色数
    uint32_t biClrImportant;  // 重要颜色数
} BMPInfoHeader;
#pragma pack(pop)

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool bmp_helper_get_size(const uint8_t *bmp_data, size_t bmp_data_size,
                         int *width, int *height)
{
    if (bmp_data == NULL || bmp_data_size < sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)) {
        ESP_LOGE(TAG, "Invalid BMP data");
        return false;
    }

    BMPFileHeader *file_header = (BMPFileHeader *)bmp_data;

    // 检查文件类型
    if (file_header->bfType != 0x4D42) {  // "BM"
        ESP_LOGE(TAG, "Not a BMP file (type: 0x%04X)", file_header->bfType);
        return false;
    }

    BMPInfoHeader *info_header = (BMPInfoHeader *)(bmp_data + sizeof(BMPFileHeader));

    // 只支持 24 位和 8 位 BMP
    if (info_header->biBitCount != 24 && info_header->biBitCount != 8) {
        ESP_LOGE(TAG, "Unsupported bit count: %d", info_header->biBitCount);
        return false;
    }

    // 只支持无压缩
    if (info_header->biCompression != 0) {
        ESP_LOGE(TAG, "Unsupported compression: %d", info_header->biCompression);
        return false;
    }

    *width = info_header->biWidth;
    *height = (info_header->biHeight < 0) ? -info_header->biHeight : info_header->biHeight;

    ESP_LOGI(TAG, "BMP size: %dx%d, bits: %d", *width, *height, info_header->biBitCount);

    return true;
}

bool bmp_helper_render(const uint8_t *bmp_data, size_t bmp_data_size,
                       int x, int y, int width, int height, bool clear_bg)
{
    if (bmp_data == NULL || bmp_data_size < sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)) {
        ESP_LOGE(TAG, "Invalid BMP data");
        return false;
    }

    BMPFileHeader *file_header = (BMPFileHeader *)bmp_data;

    // 检查文件类型
    if (file_header->bfType != 0x4D42) {
        ESP_LOGE(TAG, "Not a BMP file");
        return false;
    }

    BMPInfoHeader *info_header = (BMPInfoHeader *)(bmp_data + sizeof(BMPFileHeader));

    int src_width = info_header->biWidth;
    int src_height = (info_header->biHeight < 0) ? -info_header->biHeight : info_header->biHeight;
    bool top_down = (info_header->biHeight < 0);
    uint16_t bit_count = info_header->biBitCount;

    ESP_LOGI(TAG, "BMP: %dx%d, bits: %d, top_down: %d", src_width, src_height, bit_count, top_down);

    // 计算缩放比例 (保持宽高比)
    float scale_w = (float)width / (float)src_width;
    float scale_h = (float)height / (float)src_height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    // 如果图片比目标区域小，不放大
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    // 计算实际显示尺寸
    int actual_width = (int)(src_width * scale);
    int actual_height = (int)(src_height * scale);

    // 计算居中偏移
    int offset_x = x + (width - actual_width) / 2;
    int offset_y = y + (height - actual_height) / 2;

    ESP_LOGI(TAG, "BMP render: scale=%.2f, offset=(%d,%d), size=(%d,%d)",
             scale, offset_x, offset_y, actual_width, actual_height);

    // 清除背景
    if (clear_bg) {
        display_clear_region(x, y, width, height, COLOR_WHITE);
    }

    // 计算行字节数 (BMP 行必须是 4 字节对齐)
    int row_size = ((src_width * bit_count + 31) / 32) * 4;

    // 获取像素数据偏移
    uint32_t pixel_data_offset = file_header->bfOffBits;
    const uint8_t *pixel_data = bmp_data + pixel_data_offset;

    // 解码并绘制
    if (bit_count == 24) {
        // 24 位 BMP (RGB888)
        for (int src_y = 0; src_y < src_height; src_y++) {
            // 计算目标 Y 坐标
            int dest_y = offset_y + (int)(src_y * scale);

            // BMP 可以从下到上或从上到下存储
            int bmp_y = top_down ? src_y : (src_height - 1 - src_y);
            const uint8_t *row_data = pixel_data + bmp_y * row_size;

            for (int src_x = 0; src_x < src_width; src_x++) {
                // 计算目标 X 坐标
                int dest_x = offset_x + (int)(src_x * scale);

                // 读取 BGR 像素
                uint8_t b = row_data[src_x * 3];
                uint8_t g = row_data[src_x * 3 + 1];
                uint8_t r = row_data[src_x * 3 + 2];

                // 转换为灰度
                uint8_t gray = (r * 38 + g * 75 + b * 15) >> 7;

                // 绘制像素 (考虑缩放)
                if (scale >= 1.0f) {
                    int scale_int = (int)scale;
                    for (int sy = 0; sy < scale_int && (dest_y + sy) < (y + height); sy++) {
                        for (int sx = 0; sx < scale_int && (dest_x + sx) < (x + width); sx++) {
                            display_draw_pixel(dest_x + sx, dest_y + sy, gray);
                        }
                    }
                } else {
                    // 缩放情况: 简单采样
                    display_draw_pixel(dest_x, dest_y, gray);
                }

                // 喂狗
                if (src_x % 100 == 0) {
                    vTaskDelay(1);
                }
            }

            // 每行后喂狗
            vTaskDelay(1);
        }
    } else if (bit_count == 8) {
        // 8 位 BMP (调色板)
        ESP_LOGI(TAG, "8-bit BMP with palette");

        // 读取调色板 (256 色 x 4 字节)
        const uint8_t *palette = bmp_data + sizeof(BMPFileHeader) + info_header->biSize;

        for (int src_y = 0; src_y < src_height; src_y++) {
            int dest_y = offset_y + (int)(src_y * scale);
            int bmp_y = top_down ? src_y : (src_height - 1 - src_y);
            const uint8_t *row_data = pixel_data + bmp_y * row_size;

            for (int src_x = 0; src_x < src_width; src_x++) {
                int dest_x = offset_x + (int)(src_x * scale);

                // 读取调色板索引
                uint8_t palette_idx = row_data[src_x];

                // 从调色板读取 BGR
                uint8_t b = palette[palette_idx * 4];
                uint8_t g = palette[palette_idx * 4 + 1];
                uint8_t r = palette[palette_idx * 4 + 2];

                // 转换为灰度
                uint8_t gray = (r * 38 + g * 75 + b * 15) >> 7;

                // 绘制像素
                if (scale >= 1.0f) {
                    int scale_int = (int)scale;
                    for (int sy = 0; sy < scale_int && (dest_y + sy) < (y + height); sy++) {
                        for (int sx = 0; sx < scale_int && (dest_x + sx) < (x + width); sx++) {
                            display_draw_pixel(dest_x + sx, dest_y + sy, gray);
                        }
                    }
                } else {
                    display_draw_pixel(dest_x, dest_y, gray);
                }

                // 喂狗
                if (src_x % 100 == 0) {
                    vTaskDelay(1);
                }
            }

            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "BMP rendered successfully");
    return true;
}

bool bmp_helper_render_fullscreen(const uint8_t *bmp_data, size_t bmp_data_size)
{
    return bmp_helper_render(bmp_data, bmp_data_size,
                             0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, true);
}
