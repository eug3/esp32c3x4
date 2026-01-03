/**
 * @file image_browser.h
 * @brief 图片浏览器模块 - 支持 PNG, JPEG, BMP, GIF 等格式
 */

#ifndef IMAGE_BROWSER_H
#define IMAGE_BROWSER_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 支持的图片格式
typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_BMP,
    IMAGE_FORMAT_GIF,
} image_format_t;

// 图片信息
typedef struct {
    char file_path[256];
    image_format_t format;
    int width;
    int height;
    void *decoded_data;
    size_t data_size;
} image_info_t;

// 图片浏览器状态
typedef struct {
    image_info_t *images;
    int image_count;
    int current_index;
    int total_count;
    bool is_playing;
    lv_obj_t *image_obj;
    lv_obj_t *info_label;
    lv_obj_t *container;
} image_browser_state_t;

/**
 * @brief 初始化图片浏览器
 * @return true 成功，false 失败
 */
bool image_browser_init(void);

/**
 * @brief 扫描指定目录下的所有图片文件
 * @param directory 目录路径
 * @return 找到的图片数量
 */
int image_browser_scan_directory(const char *directory);

/**
 * @brief 加载并显示指定索引的图片
 * @param index 图片索引
 * @return true 成功，false 失败
 */
bool image_browser_show_image(int index);

/**
 * @brief 显示上一张图片
 * @return true 成功，false 失败（已是第一张）
 */
bool image_browser_prev_image(void);

/**
 * @brief 显示下一张图片
 * @return true 成功，false 失败（已是最后一张）
 */
bool image_browser_next_image(void);

/**
 * @brief 获取当前显示的图片索引
 * @return 当前索引
 */
int image_browser_get_current_index(void);

/**
 * @brief 获取图片总数
 * @return 图片总数
 */
int image_browser_get_total_count(void);

/**
 * @brief 开始幻灯片播放
 * @param interval_ms 切换间隔（毫秒）
 */
void image_browser_slideshow_start(int interval_ms);

/**
 * @brief 停止幻灯片播放
 */
void image_browser_slideshow_stop(void);

/**
 * @brief 检查是否是图片文件
 * @param filename 文件名
 * @return 图片格式
 */
image_format_t image_browser_get_image_format(const char *filename);

/**
 * @brief 清理图片浏览器资源
 */
void image_browser_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_BROWSER_H
