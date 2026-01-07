/**
 * @file png_helper.h
 * @brief PNG 图片解码和显示辅助模块 - 占位符实现
 *
 * 注意: 由于 ESP32-C3 内存限制 (仅400KB RAM), 完整的 PNG 解码
 * 可能会遇到内存不足的问题。建议:
 * 1. 使用较小的 PNG 图片 (< 100KB)
 * 2. 或者转换为 JPG/BMP 格式使用
 */

#ifndef PNG_HELPER_H
#define PNG_HELPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 获取 PNG 图片尺寸
 * @param png_data PNG 数据指针
 * @param png_data_size PNG 数据大小
 * @param width 输出: 图片宽度
 * @param height 输出: 图片高度
 * @return true 成功, false 失败
 */
bool png_helper_get_size(const uint8_t *png_data, size_t png_data_size,
                         int *width, int *height);

/**
 * @brief 渲染 PNG 图片到指定区域 (自适应缩放 + 居中)
 * @param png_data PNG 数据指针
 * @param png_data_size PNG 数据大小
 * @param x 目标区域 X 坐标
 * @param y 目标区域 Y 坐标
 * @param width 目标区域宽度
 * @param height 目标区域高度
 * @param clear_bg 是否清除背景 (true: 填充白色背景)
 * @return true 成功, false 失败
 */
bool png_helper_render(const uint8_t *png_data, size_t png_data_size,
                       int x, int y, int width, int height, bool clear_bg);

/**
 * @brief 渲染 PNG 图片 (全屏显示)
 * @param png_data PNG 数据指针
 * @param png_data_size PNG 数据大小
 * @return true 成功, false 失败
 */
bool png_helper_render_fullscreen(const uint8_t *png_data, size_t png_data_size);

#ifdef __cplusplus
}
#endif

#endif // PNG_HELPER_H
