/**
 * @file reader_screen.h
 * @brief 阅读器屏幕 - 支持 TXT 和 EPUB 格式的电子书阅读
 */

#ifndef READER_SCREEN_H
#define READER_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 支持的文件类型
typedef enum {
    BOOK_TYPE_NONE = 0,
    BOOK_TYPE_TXT,
    BOOK_TYPE_EPUB
} book_type_t;

// 阅读器设置
typedef struct {
    int font_size;            // 字体大小
    int line_spacing;         // 行间距
    int margin;               // 页边距
    bool auto_refresh;        // 自动刷新
    bool night_mode;          // 夜间模式（预留）
} reader_settings_t;

// 阅读器状态（简化版，仅供外部访问）
typedef struct reader_state_t reader_state_t;

/**
 * @brief 创建阅读器屏幕（供 screen_manager 调用）
 * @param file_path 要打开的文件路径
 * @param indev 输入设备指针
 */
void reader_screen_create_wrapper(const char *file_path, lv_indev_t *indev);

/**
 * @brief 下一页
 */
void reader_screen_next_page(void);

/**
 * @brief 上一页
 */
void reader_screen_prev_page(void);

/**
 * @brief 保存阅读进度
 * @return true 成功，false 失败
 */
bool reader_screen_save_progress(void);

/**
 * @brief 设置字体大小
 * @param font_size 字体大小
 */
void reader_screen_set_font_size(int font_size);

/**
 * @brief 获取阅读器状态
 * @return 阅读器状态指针
 */
reader_state_t* reader_screen_get_state(void);

/**
 * @brief 获取书籍类型
 * @param file_path 文件路径
 * @return 书籍类型
 */
book_type_t reader_screen_get_book_type(const char *file_path);

#ifdef __cplusplus
}
#endif

#endif // READER_SCREEN_H
