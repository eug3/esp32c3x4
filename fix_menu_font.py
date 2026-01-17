#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re

with open('main/ui/core/display_engine.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找并替换 display_get_menu_font 函数
old_func = '''sFONT* display_get_menu_font(void)
{
    // 菜单界面必须与"菜单中文高度"匹配，但不能受用户字体影响。
    // 这里按菜单中文字体高度自动挑选合适的 ASCII 字号。
    sFONT *font = choose_ascii_font_by_cjk_height_menu();
    if (font == NULL) {
        ESP_LOGE(TAG, "choose_ascii_font_by_cjk_height_menu returned NULL! Using Font16");
        return &Font16;
    }
    return font;
}'''

new_func = '''sFONT* display_get_menu_font(void)
{
    ESP_LOGI(TAG, "display_get_menu_font ENTER");
    // 菜单界面必须与"菜单中文高度"匹配，但不能受用户字体影响。
    // 这里按菜单中文字体高度自动挑选合适的 ASCII 字号。
    sFONT *font = choose_ascii_font_by_cjk_height_menu();
    ESP_LOGI(TAG, "choose_ascii_font_by_cjk_height_menu returned: %p", (void*)font);
    if (font == NULL) {
        ESP_LOGE(TAG, "choose_ascii_font_by_cjk_height_menu returned NULL! Using Font16");
        return &Font16;
    }
    ESP_LOGI(TAG, "display_get_menu_font EXIT: font=%p W=%d H=%d", (void*)font, font->Width, font->Height);
    return font;
}'''

if old_func in content:
    content = content.replace(old_func, new_func)
    with open('main/ui/core/display_engine.c', 'w', encoding='utf-8') as f:
        f.write(content)
    print("Successfully updated display_get_menu_font")
else:
    print("Could not find the exact function text")
    # Show what's around line 207
    lines = content.split('\n')
    for i, line in enumerate(lines[205:220], start=206):
        print(f"{i}: {repr(line[:80])}")
