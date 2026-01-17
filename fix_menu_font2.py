#!/usr/bin/env python3
# -*- coding: utf-8 -*-

with open('main/ui/core/display_engine.c', 'r', encoding='utf-8') as f:
    lines = f.readlines()

# 找到 display_get_menu_font 函数的起始行 (206, 0-indexed is 205)
start_idx = None
end_idx = None

for i, line in enumerate(lines):
    if 'sFONT* display_get_menu_font(void)' in line:
        start_idx = i
    if start_idx is not None and i > start_idx:
        # 找到函数结束的 }
        if line.strip() == '}' and i > start_idx + 2:
            end_idx = i
            break

if start_idx is None or end_idx is None:
    print(f"Could not find function boundaries: start={start_idx}, end={end_idx}")
    exit(1)

print(f"Found function at lines {start_idx+1} to {end_idx+1}")

# 替换为新的函数
new_func_lines = [
    'sFONT* display_get_menu_font(void)\n',
    '{\n',
    '    ESP_LOGI(TAG, "display_get_menu_font ENTER");\n',
    '    // 菜单界面必须与"菜单中文高度"匹配，但不能受用户字体影响。\n',
    '    // 这里按菜单中文字体高度自动挑选合适的 ASCII 字号。\n',
    '    sFONT *font = choose_ascii_font_by_cjk_height_menu();\n',
    '    ESP_LOGI(TAG, "choose_ascii_font_by_cjk_height_menu returned: %p", (void*)font);\n',
    '    if (font == NULL) {\n',
    '        ESP_LOGE(TAG, "choose_ascii_font_by_cjk_height_menu returned NULL! Using Font16");\n',
    '        return &Font16;\n',
    '    }\n',
    '    ESP_LOGI(TAG, "display_get_menu_font EXIT: W=%d H=%d", font->Width, font->Height);\n',
    '    return font;\n',
    '}\n',
]

# 替换
lines[start_idx:end_idx+1] = new_func_lines

with open('main/ui/core/display_engine.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)

print("Successfully updated display_get_menu_font function")
