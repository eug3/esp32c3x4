/**
 * @file jpeg_helper.h
 * @brief JPEG 解码和显示辅助模块
 */

#ifndef JPEG_HELPER_H
#define JPEG_HELPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "display_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      DEFINES
 **********************/

// JPEG 解码内存池大小 (32KB)
#define JPEG_HELPER_POOL_SIZE 32768

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief JPEG 辅助结构
 */
typedef struct {
    const uint8_t *jpeg_data;       // JPEG 数据指针
    size_t jpeg_data_size;          // JPEG 数据大小
    size_t data_pos;                // 当前读取位置

    // 显示参数
    int dest_x;                     // 目标 X 坐标
    int dest_y;                     // 目标 Y 坐标
    int dest_width;                 // 目标宽度
    int dest_height;                // 目标高度

    // 缩放参数
    float x_scale;                  // X 轴缩放比例
    float y_scale;                  // Y 轴缩放比例

    // 状态
    int last_y;                     // 上次绘制的 Y 坐标
    bool success;                   // 解码是否成功
} jpeg_helper_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 获取 JPEG 图片尺寸
 * @param jpeg_data JPEG 数据指针
 * @param jpeg_data_size JPEG 数据大小
 * @param width 输出: 图片宽度
 * @param height 输出: 图片高度
 * @return true 成功, false 失败
 */
bool jpeg_helper_get_size(const uint8_t *jpeg_data, size_t jpeg_data_size,
                          int *width, int *height);

/**
 * @brief 渲染 JPEG 图片到指定区域 (自适应缩放 + 居中)
 * @param jpeg_data JPEG 数据指针
 * @param jpeg_data_size JPEG 数据大小
 * @param x 目标区域 X 坐标
 * @param y 目标区域 Y 坐标
 * @param width 目标区域宽度
 * @param height 目标区域高度
 * @param clear_bg 是否清除背景 (true: 填充白色背景)
 * @return true 成功, false 失败
 */
bool jpeg_helper_render(const uint8_t *jpeg_data, size_t jpeg_data_size,
                        int x, int y, int width, int height, bool clear_bg);

/**
 * @brief 渲染 JPEG 图片 (全屏显示)
 * @param jpeg_data JPEG 数据指针
 * @param jpeg_data_size JPEG 数据大小
 * @return true 成功, false 失败
 */
bool jpeg_helper_render_fullscreen(const uint8_t *jpeg_data, size_t jpeg_data_size);

#ifdef __cplusplus
}
#endif

#endif // JPEG_HELPER_H
