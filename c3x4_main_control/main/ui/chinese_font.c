/**
 * @file chinese_font.c
 * @brief 内置中文字体实现
 *
 * 使用 tools/generate_chinese_font.py 生成
 * 字体: STHeiti Medium
 * 大小: 16px
 * 字符数: 约 500 常用字
 */

#include "chinese_font.h"
#include "esp_log.h"
#include <string.h>
#include "lvgl.h"

static const char *TAG = "CHINESE_FONT";

// LVGL binfont 加载函数声明
extern lv_font_t *lv_binfont_create_from_buffer(void *buffer, uint32_t size);
extern void lv_binfont_destroy(lv_font_t *font);

// 字体二进制数据 (由 EMBED_FILES 嵌入)
// ESP-IDF EMBED_FILES 生成的符号: _binary_<文件名>_bin_<start/end>
// 注意: 文件名是相对于组件目录的,路径中的 / 被移除
extern const uint8_t _binary_chinese_font_bin_start[];
extern const uint8_t _binary_chinese_font_bin_end[];

#define chinese_font_bin_data  _binary_chinese_font_bin_start
#define chinese_font_bin_size  (_binary_chinese_font_bin_end - _binary_chinese_font_bin_start)

static lv_font_t *s_chinese_font = NULL;

lv_font_t *chinese_font_get(void)
{
    if (s_chinese_font != NULL) {
        return s_chinese_font;
    }

    // 检查字体数据是否可用
    if (chinese_font_bin_size == 0) {
        ESP_LOGE(TAG, "Built-in Chinese font data not available");
        return NULL;
    }

    // 使用 LVGL 的 binfont 加载器从内存加载字体
    // 注意: 需要启用 LV_USE_FS_MEMFS
    s_chinese_font = lv_binfont_create_from_buffer((void *)chinese_font_bin_data,
                                                    chinese_font_bin_size);

    if (s_chinese_font == NULL) {
        ESP_LOGE(TAG, "Failed to load built-in Chinese font from memory (size=%u)",
                 (unsigned)chinese_font_bin_size);
        return NULL;
    }

    ESP_LOGI(TAG, "Built-in Chinese font loaded successfully from memory (size=%u bytes)",
             (unsigned)chinese_font_bin_size);

    return s_chinese_font;
}

bool chinese_font_is_available(void)
{
    return chinese_font_bin_size > 0;
}

void chinese_font_release(void)
{
    if (s_chinese_font != NULL) {
        // LVGL 9.x 不需要手动释放内置字体
        s_chinese_font = NULL;
    }
}
