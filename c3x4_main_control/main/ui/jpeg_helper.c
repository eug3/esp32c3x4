/**
 * @file jpeg_helper.c
 * @brief JPEG 解码和显示辅助模块实现
 */

#include "jpeg_helper.h"
#include "tjpgd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "JPEG_HELPER";

/**********************
 *  STATIC PROTOTYPES
 **********************/

// JPEG 数据读取回调
static size_t jpeg_input_func(JDEC *jdec, uint8_t *buff, size_t ndata);

// JPEG 像素绘制回调
static int jpeg_output_func(JDEC *jdec, void *bitmap, JRECT *rect);

/**********************
 *  STATIC VARIABLES
 **********************/

// 当前 JPEG 解码上下文 (全局变量，方便回调函数访问)
static jpeg_helper_t *g_current_context = NULL;

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief JPEG 数据读取回调函数
 */
static size_t jpeg_input_func(JDEC *jdec, uint8_t *buff, size_t ndata)
{
    jpeg_helper_t *context = (jpeg_helper_t *)jdec->device;

    // 计算剩余数据大小
    size_t remaining = context->jpeg_data_size - context->data_pos;

    // 限制读取大小
    if (ndata > remaining) {
        ndata = remaining;
    }

    // 读取数据
    if (buff != NULL) {
        memcpy(buff, context->jpeg_data + context->data_pos, ndata);
    }

    context->data_pos += ndata;

    return ndata;
}

/**
 * @brief JPEG 像素绘制回调函数
 */
static int jpeg_output_func(JDEC *jdec, void *bitmap, JRECT *rect)
{
    jpeg_helper_t *context = (jpeg_helper_t *)jdec->device;
    uint8_t *rgb = (uint8_t *)bitmap;

    // 喂狗延迟 (每行都喂狗)
    if (rect->top != context->last_y) {
        context->last_y = rect->top;
        vTaskDelay(1);  // 让出 CPU 时间，防止看门狗触发
    }

    // 逐像素绘制
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            // 读取 RGB 像素
            uint8_t r = *rgb++;
            uint8_t g = *rgb++;
            uint8_t b = *rgb++;

            // 转换为灰度 (标准公式: Gray = (R*38 + G*75 + B*15) >> 7)
            uint8_t gray = (r * 38 + g * 75 + b * 15) >> 7;

            // 计算目标坐标 (应用缩放)
            int dest_x = context->dest_x + (int)(x * context->x_scale);
            int dest_y = context->dest_y + (int)(y * context->y_scale);

            // 绘制像素 (考虑缩放)
            if (context->x_scale >= 1.0f && context->y_scale >= 1.0f) {
                // 放大: 简单地绘制多个像素
                int scale_x = (int)context->x_scale;
                int scale_y = (int)context->y_scale;

                for (int sy = 0; sy < scale_y; sy++) {
                    for (int sx = 0; sx < scale_x; sx++) {
                        display_draw_pixel(dest_x + sx, dest_y + sy, gray);
                    }
                }
            } else {
                // 缩小或不缩放: 直接绘制
                display_draw_pixel(dest_x, dest_y, gray);
            }
        }
    }

    return 1;  // 继续解码
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool jpeg_helper_get_size(const uint8_t *jpeg_data, size_t jpeg_data_size,
                          int *width, int *height)
{
    if (jpeg_data == NULL || jpeg_data_size == 0 || width == NULL || height == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for get_size");
        return false;
    }

    // 分配内存池
    void *pool = heap_caps_malloc(JPEG_HELPER_POOL_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (pool == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory pool (%d bytes)", JPEG_HELPER_POOL_SIZE);
        return false;
    }

    // 准备解码上下文
    jpeg_helper_t context = {0};
    context.jpeg_data = jpeg_data;
    context.jpeg_data_size = jpeg_data_size;
    context.data_pos = 0;

    // 准备 JPEG 解码器
    JDEC dec;
    JRESULT res = jd_prepare(&dec, jpeg_input_func, pool, JPEG_HELPER_POOL_SIZE, &context);

    if (res == JDR_OK) {
        *width = dec.width;
        *height = dec.height;
        ESP_LOGI(TAG, "JPEG size: %dx%d", *width, *height);
    } else {
        ESP_LOGE(TAG, "Failed to prepare JPEG decoder: %d", res);
    }

    heap_caps_free(pool);
    return res == JDR_OK;
}

bool jpeg_helper_render(const uint8_t *jpeg_data, size_t jpeg_data_size,
                        int x, int y, int width, int height, bool clear_bg)
{
    if (jpeg_data == NULL || jpeg_data_size == 0) {
        ESP_LOGE(TAG, "Invalid JPEG data");
        return false;
    }

    // 分配内存池
    void *pool = heap_caps_malloc(JPEG_HELPER_POOL_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (pool == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory pool (%d bytes)", JPEG_HELPER_POOL_SIZE);
        return false;
    }

    ESP_LOGI(TAG, "Allocated JPEG decode pool: %d bytes", JPEG_HELPER_POOL_SIZE);

    // 准备解码上下文
    jpeg_helper_t context = {0};
    context.jpeg_data = jpeg_data;
    context.jpeg_data_size = jpeg_data_size;
    context.data_pos = 0;
    context.dest_x = x;
    context.dest_y = y;
    context.last_y = -1;
    context.success = false;

    // 准备 JPEG 解码器
    JDEC dec;
    JRESULT res = jd_prepare(&dec, jpeg_input_func, pool, JPEG_HELPER_POOL_SIZE, &context);

    if (res != JDR_OK) {
        ESP_LOGE(TAG, "Failed to prepare JPEG decoder: %d", res);
        heap_caps_free(pool);
        return false;
    }

    ESP_LOGI(TAG, "JPEG original size: %dx%d", dec.width, dec.height);

    // 计算缩放比例 (保持宽高比)
    float scale_w = (float)width / (float)dec.width;
    float scale_h = (float)height / (float)dec.height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;  // 选择较小的比例

    // 如果图片比目标区域小，不放大 (保持原尺寸居中)
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    context.x_scale = scale;
    context.y_scale = scale;

    // 计算实际显示尺寸
    int actual_width = (int)(dec.width * scale);
    int actual_height = (int)(dec.height * scale);

    // 计算居中偏移
    int offset_x = x + (width - actual_width) / 2;
    int offset_y = y + (height - actual_height) / 2;

    context.dest_x = offset_x;
    context.dest_y = offset_y;

    ESP_LOGI(TAG, "JPEG render: scale=%.2f, offset=(%d,%d), size=(%d,%d)",
             scale, offset_x, offset_y, actual_width, actual_height);

    // 清除背景
    if (clear_bg) {
        display_clear_region(x, y, width, height, COLOR_WHITE);
    }

    // 计算硬件缩放因子 (1/1, 1/2, 1/4, 1/8)
    int scale_factor = 0;
    float temp_scale = scale;
    while (temp_scale <= 1.0f && scale_factor <= 3) {
        scale_factor++;
        temp_scale *= 2.0f;
    }
    scale_factor--;

    // 重新计算软件缩放比例
    if (scale_factor > 0) {
        context.x_scale = scale * (1 << scale_factor);
        context.y_scale = scale * (1 << scale_factor);
    }

    ESP_LOGI(TAG, "Hardware scale factor: 1/%d, software scale: %.2f",
             (1 << scale_factor), context.x_scale);

    // 设置全局上下文 (供回调函数使用)
    g_current_context = &context;

    // 解码并绘制
    res = jd_decomp(&dec, jpeg_output_func, scale_factor);

    if (res != JDR_OK) {
        ESP_LOGE(TAG, "Failed to decompress JPEG: %d", res);
    } else {
        context.success = true;
        ESP_LOGI(TAG, "JPEG decompressed successfully");
    }

    g_current_context = NULL;
    heap_caps_free(pool);

    return context.success;
}

bool jpeg_helper_render_fullscreen(const uint8_t *jpeg_data, size_t jpeg_data_size)
{
    return jpeg_helper_render(jpeg_data, jpeg_data_size,
                              0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, true);
}
