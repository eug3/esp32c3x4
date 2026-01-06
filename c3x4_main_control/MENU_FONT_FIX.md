# 菜单界面字体修复说明

## 问题描述
菜单界面受到用户设置字体的影响，当用户设置新字体后，菜单显示会乱码。这违反了设计规范：
- **菜单界面**：始终使用默认字体，不受用户设置影响
- **TXT阅读器**：优先使用用户设置的字体，失败则回退到默认

## 根本原因
`display_get_default_ascii_font()` 函数会根据当前加载的 CJK 字体动态选择 ASCII 字体，这导致菜单界面也会受到用户设置的影响。

```c
sFONT* display_get_default_ascii_font(void)
{
    return choose_ascii_font_by_cjk_height();  // 会查询当前加载的 CJK 字体高度
}
```

## 解决方案

### 1. 添加专用的菜单字体函数

在 `display_engine.h` 中添加新函数声明：
```c
/**
 * @brief 获取菜单专用字体（固定的，始终不受用户设置的字体影响）
 *
 * 菜单界面必须使用此函数获取字体，以确保菜单显示不会因为用户设置字体而乱码。
 * 返回值总是一个固定的内置 ASCII 字体（如 Font12）。
 */
sFONT* display_get_menu_font(void);
```

### 2. 实现菜单字体函数

在 `display_engine.c` 中实现：
```c
sFONT* display_get_menu_font(void)
{
    // 菜单界面必须使用固定的默认字体，不受用户设置的字体影响
    // 始终返回 Font12 作为菜单的标准字体
    return &Font12;
}
```

### 3. 更新所有菜单屏幕

将所有菜单相关屏幕的 `display_get_default_ascii_font()` 替换为 `display_get_menu_font()`：

#### 修改的文件：
1. **file_browser_screen.c** - 文件浏览器菜单
   - `draw_single_file()`
   - `draw_page_indicator()`
   - `on_draw()`

2. **home_screen.c** - 首页菜单
   - `draw_text_utf8()` - fallback 字体
   - `on_draw()` - 菜单标题和电池信息
   - `draw_menu_item()` - 菜单项绘制

3. **font_select_screen.c** - 字体选择菜单
   - `draw_font_item()`
   - `show_restart_dialog()`
   - `on_draw()`

4. **settings_screen_simple.c** - 设置菜单
   - `draw_setting_item()`
   - `on_draw()`

5. **ble_reader_screen.c** - BLE 阅读器菜单
   - `on_draw()`

6. **image_viewer_screen.c** - 图像查看器菜单
   - PNG 格式错误提示
   - 不支持格式提示
   - "没有图片"占位符

## 保留不变的部分
**reader_screen_simple.c** 中的 `display_get_default_ascii_font()` 调用保持不变，因为：
- 这些是内容显示（阅读器相关），而不是菜单界面
- 阅读器应该随着用户设置的字体而调整 ASCII 字体

## 规范遵守

修改后的代码遵循如下规范：

| 模块 | 字体来源 | 使用方式 |
|-----|--------|--------|
| 菜单界面 | 默认 Font12 | ✅ 始终用 `display_get_menu_font()` |
| TXT阅读器（无设置） | 默认字体 | ✅ 用 `display_get_default_ascii_font()` 自动适配 |
| TXT阅读器（有设置） | 用户设置的字体 | ✅ NVS 字体 + 新字体缓存，失败则回退默认 |

## 编译验证
✅ 项目成功编译，无错误警告

```
Project build complete.
```

## 修改时间
2026-01-06
