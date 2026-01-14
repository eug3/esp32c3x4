/**
 * @file bmp_helper.h
 * @brief BMP 图片解码和显示辅助模块
 */

#ifndef BMP_HELPER_H
#define BMP_HELPER_H

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
 * @brief 获取 BMP 图片尺寸
 * @param bmp_data BMP 数据指针
 * @param bmp_data_size BMP 数据大小
 * @param width 输出: 图片宽度
 * @param height 输出: 图片高度
 * @return true 成功, false 失败
 */
bool bmp_helper_get_size(const uint8_t *bmp_data, size_t bmp_data_size,
                         int *width, int *height);

/**
 * @brief 渲染 BMP 图片到指定区域 (自适应缩放 + 居中)
 * @param bmp_data BMP 数据指针
 * @param bmp_data_size BMP 数据大小
 * @param x 目标区域 X 坐标
 * @param y 目标区域 Y 坐标
 * @param width 目标区域宽度
 * @param height 目标区域高度
 * @param clear_bg 是否清除背景 (true: 填充白色背景)
 * @return true 成功, false 失败
 */
bool bmp_helper_render(const uint8_t *bmp_data, size_t bmp_data_size,
                       int x, int y, int width, int height, bool clear_bg);

/**
 * @brief 渲染 BMP 图片 (全屏显示)
 * @param bmp_data BMP 数据指针
 * @param bmp_data_size BMP 数据大小
 * @return true 成功, false 失败
 */
bool bmp_helper_render_fullscreen(const uint8_t *bmp_data, size_t bmp_data_size);

#ifdef __cplusplus
}
#endif

#endif // BMP_HELPER_H
