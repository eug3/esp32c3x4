# LVGL GUI 集成指南

## 概述

本项目已成功集成 LVGL (Light and Versatile Graphics Library) v8.3，用于在 Xteink X4 的 4.26" EPD 电子纸显示屏上创建图形用户界面。

## 文件结构

```
main/
├── lv_conf.h              # LVGL配置文件
├── lvgl_driver.c          # LVGL显示和输入驱动
├── lvgl_driver.h          # 驱动头文件
├── lvgl_demo.c            # 示例GUI应用
├── lvgl_demo.h            # 示例GUI头文件
├── main_lvgl_example.c    # 集成示例代码
├── idf_component.yml      # 组件依赖（包含LVGL）
└── CMakeLists.txt         # 构建配置
```

## 快速开始

### 1. 构建项目

```bash
cd /Users/beijihu/Github/esp32c3x4/c3x4_main_control
idf.py build
```

项目会自动下载 LVGL 组件依赖。

### 2. 在 main.c 中集成 LVGL

在 `main.c` 顶部添加头文件：

```c
#include "lvgl_driver.h"
#include "lvgl_demo.h"
```

在 `app_main()` 函数中添加初始化代码：

```c
void app_main(void)
{
    // ... 现有的初始化代码 ...
    
    // 确保EPD已初始化
    EPD_4IN26_Init();
    Paint_NewImage(BlackImage, EPD_4IN26_WIDTH, EPD_4IN26_HEIGHT, 270, WHITE);
    Paint_Clear(WHITE);
    
    // 初始化LVGL
    ESP_LOGI("MAIN", "Initializing LVGL...");
    lv_disp_t *disp = lvgl_display_init();
    lv_indev_t *indev = lvgl_input_init();
    
    // 创建LVGL任务
    xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 5, NULL);
    xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 5, NULL);
    
    // 创建UI界面
    lvgl_demo_create_menu_screen();
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
    
    // ... 其余代码 ...
}
```

### 3. 烧录和运行

```bash
idf.py flash monitor
```

## 功能特性

### 已实现的功能

1. **显示驱动** (`lvgl_driver.c`)
   - EPD 4.26" (800x480) 单色显示支持
   - 双缓冲机制
   - 自动刷新管理

2. **输入驱动** (`lvgl_driver.c`)
   - 6个物理按键支持
   - 按键到LVGL键盘事件的映射
   - 事件队列管理

3. **示例UI** (`lvgl_demo.c`)
   - 启动画面 (Splash Screen)
   - 主屏幕 (带交互按钮)
   - 菜单屏幕 (可导航列表)
   - 进度条示例
   - 信息显示屏幕

### 按键映射

| 物理按键 | LVGL键值 | 用途 |
|---------|---------|------|
| CONFIRM | LV_KEY_ENTER | 确认/选择 |
| BACK | LV_KEY_ESC | 返回/取消 |
| LEFT | LV_KEY_LEFT | 向左导航 |
| RIGHT | LV_KEY_RIGHT | 向右导航 |
| VOLUME_UP | LV_KEY_UP | 向上导航 |
| VOLUME_DOWN | LV_KEY_DOWN | 向下导航 |

## 配置说明

### lv_conf.h 关键配置

```c
// 分辨率
#define LV_HOR_RES_MAX  800
#define LV_VER_RES_MAX  480

// 颜色深度（单色）
#define LV_COLOR_DEPTH  1

// 内存池大小
#define LV_MEM_SIZE     (50U * 1024U)  // 50KB

// 刷新周期
#define LV_DISP_DEF_REFR_PERIOD  30  // ms

// 输入设备读取周期
#define LV_INDEV_DEF_READ_PERIOD 30  // ms
```

## 创建自定义界面

### 基本示例

```c
void create_my_screen(void)
{
    // 创建屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 添加标题
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "My Screen");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 添加按钮
    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, my_btn_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Click Me");
    lv_obj_center(label);
    
    // 刷新显示
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
}

// 事件处理
void my_btn_event(lv_event_t *e)
{
    ESP_LOGI("UI", "Button clicked!");
    // 更新UI...
    lvgl_display_refresh();  // 刷新EPD
}
```

### 可用的UI组件

LVGL提供了丰富的UI组件（在 `lv_conf.h` 中已启用）：

- **基础**: Label, Button, Image, Line
- **容器**: List, Menu, Tabview, Tileview
- **输入**: Slider, Switch, Checkbox, Textarea
- **显示**: Bar, Meter, Chart, Canvas
- **其他**: Dropdown, Roller, Spinner, Msgbox

示例请参考 `lvgl_demo.c` 中的实现。

## EPD刷新优化

### 刷新策略

EPD刷新较慢（~2秒），建议采用事件驱动刷新：

```c
static bool epd_needs_refresh = false;

// 在UI事件中标记需要刷新
void ui_event_handler(lv_event_t *e)
{
    // 处理事件
    // ...
    
    // 标记需要刷新
    epd_needs_refresh = true;
}

// 在主循环中检查并刷新
void main_loop(void)
{
    while (1) {
        if (epd_needs_refresh) {
            lvgl_display_refresh();
            epd_needs_refresh = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 部分刷新（可选）

如果EPD驱动支持部分刷新，可以修改 `disp_flush_cb()` 实现更快的局部更新。

## 字体支持

### 当前可用字体

- Montserrat 8, 12, 14, 16, 24

### 添加中文字体

1. 使用 LVGL 字体转换器: https://lvgl.io/tools/fontconverter
2. 生成 `.c` 字体文件
3. 添加到项目并在 `lv_conf.h` 中启用
4. 在代码中使用：

```c
LV_FONT_DECLARE(my_chinese_font_16);
lv_obj_set_style_text_font(label, &my_chinese_font_16, 0);
```

## 内存管理

### 当前配置

- LVGL 内存池: 50KB
- 显示缓冲: 2 x (800 x 10 x 1bit) = ~2KB

### 内存不足？

如果遇到内存分配失败，可以：

1. 增加 `LV_MEM_SIZE` (在 `lv_conf.h`)
2. 减小显示缓冲行数 (在 `lvgl_driver.c`)
3. 禁用不需要的UI组件 (在 `lv_conf.h`)

## 调试技巧

### 启用详细日志

在 `lv_conf.h` 中：

```c
#define LV_LOG_LEVEL  LV_LOG_LEVEL_TRACE
```

### 监控内存使用

```c
lv_mem_monitor_t mon;
lv_mem_monitor(&mon);
ESP_LOGI("LVGL", "Memory: %d%% used", mon.used_pct);
```

### 性能监控

在 `lv_conf.h` 中启用：

```c
#define LV_USE_PERF_MONITOR  1
```

## 示例应用

运行预定义的示例：

```c
// 启动画面
lvgl_demo_create_splash_screen();
lvgl_display_refresh();

// 菜单
lvgl_demo_create_menu_screen();
lvgl_display_refresh();

// 进度条
lvgl_demo_create_progress_screen();
lvgl_display_refresh();

// 信息页面
lvgl_demo_create_info_screen("About", "Version 1.0");
lvgl_display_refresh();
```

## 后续优化方向

- [ ] 添加中文字体支持
- [ ] 实现文件浏览器界面
- [ ] 添加设置页面
- [ ] 集成电池状态显示
- [ ] BLE配对和控制界面
- [ ] 自定义EPD优化主题
- [ ] 实现部分刷新优化
- [ ] 添加更多示例应用

## 参考资源

- [LVGL官方文档](https://docs.lvgl.io/8.3/)
- [LVGL示例](https://docs.lvgl.io/8.3/examples.html)
- [LVGL字体转换器](https://lvgl.io/tools/fontconverter)
- [ESP-IDF LVGL组件](https://components.espressif.com/components/lvgl/lvgl)

## 故障排除

### 问题：编译错误 "lvgl.h not found"

**解决**：运行 `idf.py reconfigure` 重新配置项目，让组件管理器下载依赖。

### 问题：显示空白

**检查**：
1. EPD是否正确初始化
2. `BlackImage` 缓冲区是否已分配
3. 调用 `lvgl_display_refresh()` 刷新显示

### 问题：按键无响应

**检查**：
1. `get_pressed_button()` 函数是否正常工作
2. LVGL输入设备驱动是否已注册
3. UI对象是否设置为可聚焦 (`lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE)`)

### 问题：内存不足

**解决**：
1. 增加 `LV_MEM_SIZE`
2. 减少缓冲区大小
3. 禁用不需要的LVGL功能

## 许可证

LVGL使用MIT许可证。
