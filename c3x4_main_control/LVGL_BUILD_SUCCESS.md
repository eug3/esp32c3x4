# LVGL GUI 集成 - 快速指南

## ✅ 编译成功！

项目已成功集成LVGL并编译通过。现在可以在您的代码中使用LVGL来创建GUI。

## 📝 重要说明

### 配置方式

ESP-IDF的LVGL组件使用**Kconfig配置系统**，不需要手动的`lv_conf.h`文件。

配置LVGL请使用：
```bash
idf.py menuconfig
# 进入 Component config → LVGL configuration
```

### 默认配置

当前LVGL使用以下默认配置：
- 颜色深度：根据menuconfig设置
- 内存大小：根据menuconfig设置
- 字体：montserrat_14（默认）

## 🚀 如何使用

### 1. 在main.c中包含头文件

```c
#include "lvgl_driver.h"
#include "lvgl_demo.h"
```

### 2. 初始化LVGL（在app_main中）

```c
// 确保EPD已初始化
EPD_4IN26_Init();
Paint_NewImage(BlackImage, EPD_4IN26_WIDTH, EPD_4IN26_HEIGHT, 270, WHITE);
Paint_Clear(WHITE);

// 初始化LVGL
lv_disp_t *disp = lvgl_display_init();
lv_indev_t *indev = lvgl_input_init();

// 创建LVGL任务
xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 5, NULL);
xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 5, NULL);

// 创建UI（选择一个）
lvgl_demo_create_splash_screen();  // 启动画面
// lvgl_demo_create_menu_screen();     // 菜单
// lvgl_demo_create_main_screen();     // 主屏幕
// lvgl_demo_create_progress_screen(); // 进度条

// 刷新EPD
vTaskDelay(pdMS_TO_TICKS(100));
lvgl_display_refresh();
```

### 3. 创建自定义UI

```c
void my_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
}
```

## 📚 可用文件

- **lvgl_driver.c/h** - 显示和输入驱动
- **lvgl_demo.c/h** - 5个示例UI界面
- **main_lvgl_example.c** - 详细集成示例（仅作参考，未编译）

## 🎮 按键映射

| 按键 | LVGL键值 | 功能 |
|-----|---------|------|
| CONFIRM | LV_KEY_ENTER | 确认 |
| BACK | LV_KEY_ESC | 返回 |
| LEFT | LV_KEY_LEFT | 向左 |
| RIGHT | LV_KEY_RIGHT | 向右 |
| VOLUME_UP | LV_KEY_UP | 向上 |
| VOLUME_DOWN | LV_KEY_DOWN | 向下 |

## ⚙️ LVGL配置

要修改LVGL配置（字体、颜色、内存等）：

```bash
idf.py menuconfig
```

导航到：**Component config → LVGL configuration**

可配置项：
- Color depth (颜色深度)
- Memory size (内存大小)
- Fonts (字体)
- Widgets (组件)
- Themes (主题)

## 🔧 常见问题

### Q: 如何启用更多字体？
**A**: `idf.py menuconfig` → Component config → LVGL configuration → Font usage

### Q: 显示空白？
**A**: 确保调用了 `lvgl_display_refresh()`

### Q: 内存不足？
**A**: `idf.py menuconfig` → Component config → LVGL configuration → Memory Settings → Heap size

### Q: 如何调试？
**A**: 启用LVGL日志：menuconfig → LVGL configuration → Log Settings

## 📖 参考文档

- 完整文档: [LVGL_INTEGRATION_README.md](LVGL_INTEGRATION_README.md)
- 快速上手: [LVGL_QUICKSTART.md](LVGL_QUICKSTART.md)
- 集成示例: [main_lvgl_example.c](main/main_lvgl_example.c)
- LVGL官方: https://docs.lvgl.io/8.3/

## 🎯 下一步

1. ✅ 编译成功
2. 📝 在main.c中添加初始化代码
3. 🔨 烧录测试：`idf.py flash monitor`
4. 🎨 创建自定义界面
5. 🌐 添加中文字体支持（可选）

---

**提示**: 参考 `main_lvgl_example.c` 获取完整的集成示例代码！
