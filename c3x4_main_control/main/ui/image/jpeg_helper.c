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
 * @brief JPEG 像素绘制回调函数（优化版本）
 */
static int jpeg_output_func(JDEC *jdec, void *bitmap, JRECT *rect)
{
    jpeg_helper_t *context = (jpeg_helper_t *)jdec->device;
    uint8_t *rgb = (uint8_t *)bitmap;

    // 喂狗延迟优化：每 16 行才喂狗一次（而不是每行）
    if (rect->top != context->last_y) {
        if ((rect->top & 0xF) == 0) {  // 每 16 行检查一次
            context->last_y = rect->top;
            vTaskDelay(1);  // 让出 CPU 时间，防止看门狗触发
        }
    }

    // 获取 framebuffer 直接访问（避免锁开销）
    extern uint8_t* display_get_framebuffer(void);
    uint8_t *fb = display_get_framebuffer();

    // 优化：批量处理像素，减少函数调用开销
    const int rect_width = rect->right - rect->left + 1;
    const int rect_height = rect->bottom - rect->top + 1;

    for (int y = 0; y < rect_height; y++) {
        const int src_y = rect->top + y;
        const int dest_y = context->dest_y + (int)(src_y * context->y_scale);

        for (int x = 0; x < rect_width; x++) {
            const int src_x = rect->left + x;

            // 读取 RGB 像素
            uint8_t r = *rgb++;
            uint8_t g = *rgb++;
            uint8_t b = *rgb++;

            // 快速灰度转换 (优化公式: Gray = (R*77 + G*150 + B*29) >> 8)
            uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

            // 阈值转换为黑白
            uint8_t bw = (gray < 128) ? 0 : 1;

            // 计算480x800逻辑坐标
            const int dest_x = context->dest_x + (int)(src_x * context->x_scale);

            // 边界检查（480x800逻辑）
            if (dest_x >= 0 && dest_x < 480 && dest_y >= 0 && dest_y < 800) {
                // 转换到800x480物理坐标（ROTATE_270: X_phys=Y_logic, Y_phys=479-X_logic）
                const int phys_x = dest_y;
                const int phys_y = 479 - dest_x;

                // 写入800x480物理帧缓冲
                const uint32_t byte_idx = phys_y * 100 + (phys_x / 8);  // 100 = 800/8
                const uint8_t bit_mask = 0x80 >> (phys_x % 8);

                if (bw == 0) {
                    fb[byte_idx] &= ~bit_mask;  // 黑色
                } else {
                    fb[byte_idx] |= bit_mask;   // 白色
                }
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

/**
 * @brief 计算 JPEG 解码所需的最小内存池大小
 * 根据 TJPGD 文档，计算公式基于图片尺寸和 MCU 大小
 */
static size_t calculate_required_pool_size(int width, int height, uint8_t ncomp, uint8_t msx, uint8_t msy)
{
    // 基础大小: 输入缓冲区 + 霍夫曼表 + 量化表
    size_t base_size = 512 + 256 + 64;

    // MCU 相关缓冲区
    // workbuf: n * 64 * 2 + 64 (至少 256 字节)
    // mcubuf: (n + 2) * 64 * sizeof(jd_yuv_t)
    // 对于 JD_FASTDECODE=1, jd_yuv_t = int16_t (2 bytes)
    size_t n = msx * msy;
    size_t workbuf_size = n * 128 + 64;
    if (workbuf_size < 256) workbuf_size = 256;

    size_t mcubuf_size = (n + 2) * 64 * 2;  // int16_t = 2 bytes

    // 额外余量
    size_t extra = 256;

    return base_size + workbuf_size + mcubuf_size + extra;
}

bool jpeg_helper_render(const uint8_t *jpeg_data, size_t jpeg_data_size,
                        int x, int y, int width, int height, bool clear_bg)
{
    if (jpeg_data == NULL || jpeg_data_size == 0) {
        ESP_LOGE(TAG, "Invalid JPEG data");
        return false;
    }

    // 首先获取图片尺寸
    int img_width, img_height;
    if (!jpeg_helper_get_size(jpeg_data, jpeg_data_size, &img_width, &img_height)) {
        ESP_LOGE(TAG, "Failed to get JPEG size");
        return false;
    }

    // 计算所需内存池大小
    // 默认使用 32KB，对于大多数图片足够
    size_t required_pool = calculate_required_pool_size(img_width, img_height, 3, 1, 1);
    if (required_pool < 4096) required_pool = 4096;  // 最小 4KB

    // 尝试分配内存池（从 32KB 开始尝试，不够则递减）
    size_t pool_sizes[] = {32768, 24576, 16384, 12288, 8192, 6144, 4096};
    size_t num_sizes = sizeof(pool_sizes) / sizeof(pool_sizes[0]);

    void *pool = NULL;
    size_t allocated_size = 0;

    for (size_t i = 0; i < num_sizes; i++) {
        if (pool_sizes[i] < required_pool) {
            ESP_LOGW(TAG, "Pool size %d < required %d, skipping", pool_sizes[i], required_pool);
            continue;
        }

        pool = heap_caps_malloc(pool_sizes[i], MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (pool != NULL) {
            allocated_size = pool_sizes[i];
            ESP_LOGI(TAG, "Allocated JPEG decode pool: %d bytes (required: %d)", allocated_size, required_pool);
            break;
        }

        ESP_LOGW(TAG, "Failed to allocate %d bytes, trying smaller...", pool_sizes[i]);
    }

    if (pool == NULL) {
        ESP_LOGE(TAG, "Failed to allocate any memory pool for JPEG decoding");
        return false;
    }

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
    JRESULT res = jd_prepare(&dec, jpeg_input_func, pool, allocated_size, &context);

    if (res != JDR_OK) {
        ESP_LOGE(TAG, "Failed to prepare JPEG decoder: %d", res);
        heap_caps_free(pool);
        return false;
    }

    ESP_LOGI(TAG, "JPEG original size: %dx%d", dec.width, dec.height);

    // 计算缩放比例 (Cover模式：至少一边铺满，居中裁剪)
    float scale_w = (float)width / (float)dec.width;
    float scale_h = (float)height / (float)dec.height;
    float scale = (scale_w > scale_h) ? scale_w : scale_h;  // Cover模式：选择较大比例

    // 如果图片比目标区域小，不放大 (保持原尺寸居中)
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    context.x_scale = scale;
    context.y_scale = scale;

    // 计算实际显示尺寸
    int actual_width = (int)(dec.width * scale);
    int actual_height = (int)(dec.height * scale);

    // 计算居中偏移（Cover模式：可能超出边界，会被裁剪）
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
