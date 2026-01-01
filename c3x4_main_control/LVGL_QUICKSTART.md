# LVGL GUI 快速上手指南

## ✅ 已完成的工作

LVGL已成功集成到您的ESP32-C3项目中！以下是已创建的文件：

### 核心文件
1. **lv_conf.h** - LVGL配置（800x480单色显示，50KB内存）
2. **lvgl_driver.c/h** - EPD显示驱动和按键输入驱动
3. **lvgl_demo.c/h** - 5个示例UI界面
4. **main_lvgl_example.c** - 集成代码示例
5. **idf_component.yml** - 已添加LVGL依赖
6. **CMakeLists.txt** - 已更新构建配置

## 🚀 快速开始（3步）

### 步骤1: 构建项目

首次构建会自动下载LVGL组件：

```bash
cd /Users/beijihu/Github/esp32c3x4/c3x4_main_control
idf.py build
```

### 步骤2: 修改 main.c

在 `main.c` 顶部添加：

```c
#include "lvgl_driver.h"
#include "lvgl_demo.h"
```

在 `app_main()` 中找到EPD初始化的位置，添加：

```c
// 初始化LVGL
ESP_LOGI("MAIN", "Initializing LVGL...");
lv_disp_t *disp = lvgl_display_init();
lv_indev_t *indev = lvgl_input_init();

// 创建LVGL任务
xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 5, NULL);
xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 5, NULL);

// 创建菜单界面
lvgl_demo_create_menu_screen();
vTaskDelay(pdMS_TO_TICKS(100));
lvgl_display_refresh();
```

### 步骤3: 烧录并测试

```bash
idf.py flash monitor
```

按下设备上的按键（UP/DOWN/CONFIRM）来导航菜单！

## 📱 5个预置界面

您可以尝试以下任何一个示例界面：

```c
// 1. 启动画面（简洁的欢迎界面）
lvgl_demo_create_splash_screen();

// 2. 主屏幕（带可点击按钮）
lvgl_demo_create_main_screen();

// 3. 菜单界面（可导航列表）⭐ 推荐
lvgl_demo_create_menu_screen();

// 4. 进度条示例（电量、存储等）
lvgl_demo_create_progress_screen();

// 5. 信息显示（文本内容）
lvgl_demo_create_info_screen("标题", "内容文本...");

// 记得在创建界面后刷新EPD
vTaskDelay(pdMS_TO_TICKS(100));
lvgl_display_refresh();
```

## 🎮 按键控制

| 按键 | 功能 |
|-----|------|
| ⬆️ VOLUME_UP | 向上移动 |
| ⬇️ VOLUME_DOWN | 向下移动 |
| ⬅️ LEFT | 向左移动 |
| ➡️ RIGHT | 向右移动 |
| ✅ CONFIRM | 确认/选择 |
| ❌ BACK | 返回/取消 |

## 💡 创建您自己的界面

### 最简示例（10行代码）

```c
void my_simple_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
}
```

### 带按钮的界面

```c
void btn_event(lv_event_t *e) {
    ESP_LOGI("UI", "按钮被点击了！");
    lvgl_display_refresh();
}

void my_button_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    
    // 创建按钮
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, btn_event, LV_EVENT_CLICKED, NULL);
    
    // 按钮文字
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "点我");
    lv_obj_center(label);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_display_refresh();
}
```

## 🎨 可用UI组件

LVGL提供了丰富的组件（都已启用）：

- **文本**: Label, Textarea
- **按钮**: Button, Checkbox, Switch
- **容器**: List, Menu, Tabview
- **进度**: Bar, Slider, Spinner
- **其他**: Dropdown, Roller, Canvas, QR Code

查看 [lvgl_demo.c](main/lvgl_demo.c) 了解使用示例。

## ⚡ EPD刷新提示

EPD刷新较慢（~2秒），建议：

1. **仅在需要时刷新**（按键按下后）
2. **避免频繁刷新**（使用标志位）
3. **等待LVGL渲染完成**（vTaskDelay 100ms）

```c
// 推荐的刷新模式
lv_label_set_text(my_label, "新文本");  // 修改UI
vTaskDelay(pdMS_TO_TICKS(100));        // 等待渲染
lvgl_display_refresh();                 // 刷新EPD
```

## 📚 更多资源

- **详细文档**: [LVGL_INTEGRATION_README.md](LVGL_INTEGRATION_README.md)
- **集成示例**: [main_lvgl_example.c](main/main_lvgl_example.c)
- **LVGL官方文档**: https://docs.lvgl.io/8.3/
- **示例代码**: [lvgl_demo.c](main/lvgl_demo.c)

## 🔧 常见问题

### Q: 编译找不到 lvgl.h？
**A**: 运行 `idf.py reconfigure` 重新配置项目

### Q: 显示空白？
**A**: 确保调用了 `lvgl_display_refresh()`

### Q: 按键没反应？
**A**: 检查LVGL输入驱动是否初始化：`lvgl_input_init()`

### Q: 内存不足？
**A**: 增加 `lv_conf.h` 中的 `LV_MEM_SIZE`

## 🎯 下一步

1. ✅ 测试示例界面
2. ✅ 尝试修改界面文字
3. ✅ 创建自己的界面
4. 📝 添加中文字体支持
5. 🔋 集成电池状态显示
6. 📁 实现文件浏览器

---

**🎉 恭喜！您已经完成了LVGL的集成！**

现在可以开始创建您自己的精美GUI界面了！
