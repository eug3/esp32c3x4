#!/usr/bin/env python3
"""
将生成的 .bin 字体文件转换为 C 代码并更新到项目中
"""
import os
import sys

def bin_to_c_source(bin_path, output_path):
    """将 bin 文件转换为 C 源文件"""

    with open(bin_path, 'rb') as f:
        data = f.read()

    size = len(data)
    var_name = os.path.basename(bin_path).replace('.', '_')

    with open(output_path, 'w') as f:
        f.write(f'''/**
 * @file {os.path.basename(output_path)}
 * @brief 内置中文字体实现
 *
 * 使用 tools/generate_chinese_font.py 生成
 * 字体: STHeiti Medium
 * 大小: 16px
 * 字符数: 约 500 常用字
 * 数据大小: {size} bytes
 */

#include "chinese_font.h"
#include "esp_log.h"
#include <string.h>
#include "lvgl/lvgl.h"

static const char *TAG = "CHINESE_FONT";

// 字体二进制数据 (LVGL binfont 格式)
const uint8_t chinese_font_data[{size}] = {{
''')

        # 每16字节一行
        for i in range(0, size, 16):
            chunk = data[i:i+16]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f'    {hex_str},\n')

        f.write('''
};

const uint32_t chinese_font_data_len = ''' + str(size) + ''';

static lv_font_t *s_chinese_font = NULL;

lv_font_t *chinese_font_get(void)
{{
    if (s_chinese_font != NULL) {{
        return s_chinese_font;
    }}

    // 使用 LVGL binfont_loader 加载字体数据
    extern lv_font_t *lv_binfont_create(const void *src, uint32_t size);

    s_chinese_font = lv_binfont_create(chinese_font_data, chinese_font_data_len);
    if (s_chinese_font == NULL) {{
        ESP_LOGE(TAG, "Failed to load built-in Chinese font");
        return NULL;
    }}

    ESP_LOGI(TAG, "Built-in Chinese font loaded successfully (%d bytes)", chinese_font_data_len);
    return s_chinese_font;
}}

bool chinese_font_is_available(void)
{{
    return chinese_font_data_len > 0;
}}

void chinese_font_release(void)
{{
    if (s_chinese_font != NULL) {{
        // LVGL 9.x 不需要手动释放内置字体
        s_chinese_font = NULL;
    }}
}}
''')

    print(f"Generated: {output_path} ({size} bytes)")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 update_chinese_font.py <font.bin>")
        print("Example: python3 update_chinese_font.py chinese_font.bin")
        sys.exit(1)

    bin_path = sys.argv[1]
    if not os.path.exists(bin_path):
        print(f"Error: File not found: {bin_path}")
        sys.exit(1)

    # 生成到项目目录
    output_path = '../main/ui/chinese_font.c'
    bin_to_c_source(bin_path, output_path)
    print(f"\nDone! Updated {output_path}")
