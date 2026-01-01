# LVGL GUI 集成文档

## 概述

已成功将 **LVGL 8.3** 图形库集成到 Xteink X4 墨水屏设备中，提供现代化的 GUI 界面。

## 架构设计

### 1. 驱动适配层

**文件**: [main/lvgl_driver.c](main/lvgl_driver.c) / [main/lvgl_driver.h](main/lvgl_driver.h)

#### 显示驱动
- **显示缓冲**: 双缓冲机制，每次处理 10 行像素
- **刷新回调**: `disp_flush_cb()` 将 LVGL 渲染结果写入 EPD 缓冲区
- **像素对齐**: 8 像素对齐（EPD 硬件要求）
- **刷新模式**: 完整刷新模式（`full_refresh = 1`）

```c
// 显示初始化
lv_disp_t* lvgl_display_init(void);

// 强制刷新 EPD（将缓冲区发送到硬件）
void lvgl_display_refresh(void);
```

#### 输入驱动
- **设备类型**: 键盘输入（`LV_INDEV_TYPE_KEYPAD`）
- **按键映射**:
  | 物理按键 | LVGL 按键 |
  |---------|-----------|
  | CONFIRM | LV_KEY_ENTER |
  | BACK | LV_KEY_ESC |
  | LEFT | LV_KEY_LEFT |
  | RIGHT | LV_KEY_RIGHT |
  | VOLUME_UP | LV_KEY_UP |
  | VOLUME_DOWN | LV_KEY_DOWN |

- **读取回调**: `keypad_read_cb()` 每 30ms 轮询按键状态

```c
// 输入设备初始化
lv_indev_t* lvgl_input_init(void);
```

### 2. UI 示例界面

**文件**: [main/lvgl_demo.c](main/lvgl_demo.c) / [main/lvgl_demo.h](main/lvgl_demo.h)

#### 启动画面
```c
void lvgl_demo_create_splash_screen(void);
```
- 显示设备名称和版本信息
- 启动时展示 2 秒

#### 主菜单界面
```c
void lvgl_demo_create_menu_screen(void);
```
- 列表形式的菜单
- 包含 5 个菜单项：
  - Settings（设置）
  - File Browser（文件浏览器）
  - Network（网络）
  - Battery Info（电池信息）
  - About（关于）

#### 主屏幕（交互示例）
```c
void lvgl_demo_create_main_screen(void);
```
- 标题 + 按钮交互演示
- 点击按钮会更新文本

#### 进度条界面
```c
void lvgl_demo_create_progress_screen(void);
```
- 电池电量进度条
- 存储空间进度条
- 亮度调节滑块

#### 信息显示界面
```c
void lvgl_demo_create_info_screen(const char *title, const char *info_text);
```
- 通用信息展示界面
- 支持长文本滚动

### 3. 系统集成

**文件**: [main/main.c](main/main.c#L1447-L1486)

#### 初始化流程

```c
void app_main(void) {
    // ... 硬件初始化 ...
    
    // 1. 分配 EPD 缓冲区
    UWORD Imagesize = ((EPD_WIDTH / 8) + (EPD_WIDTH % 8 ? 1 : 0)) * EPD_HEIGHT;
    BlackImage = (UBYTE *)malloc(Imagesize);
    Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
    
    // 2. 初始化 LVGL 显示驱动
    lvgl_display_init();
    
    // 3. 初始化输入设备（按键）
    lvgl_input_init();
    
    // 4. 创建 LVGL 任务
    xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 5, NULL);
    xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 4, NULL);
    
    // 5. 显示启动画面
    lvgl_demo_create_splash_screen();
    lvgl_display_refresh();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 6. 显示主菜单
    lvgl_demo_create_menu_screen();
    lvgl_display_refresh();
}
```

#### LVGL 任务

**Tick 任务** (优先级 5)
- 每 10ms 调用 `lv_tick_inc(10)`
- 为 LVGL 提供时基

**定时器任务** (优先级 4)
- 每 5ms 调用 `lv_timer_handler()`
- 处理 UI 更新、动画、事件

## 配置要点

### LVGL 配置

通过 ESP-IDF Kconfig 配置（`idf.py menuconfig`）:

```
Component config → LVGL configuration
├── Color depth: 1 bit monochrome
├── DPI: 100
├── Default font: lv_font_montserrat_14
└── Full refresh: Enabled
```

**注意**: 不要手动创建 `lv_conf.h`，使用 Kconfig 配置！

### 分区表

[partitions.csv](partitions.csv):
```csv
factory,  app,  factory, 0x10000, 0x1C0000,  # 1.75MB（LVGL需要）
littlefs, data, spiffs,  0x1D0000, 0x30000,  # 192KB
```

LVGL 增加了应用程序大小：
- **无 LVGL**: ~1.2MB
- **有 LVGL**: ~1.4MB
- **分区大小**: 1.75MB（留有 23% 余量）

### 依赖配置

[main/idf_component.yml](main/idf_component.yml):
```yaml
dependencies:
  lvgl__lvgl:
    version: "~8.3.0"
```

## 使用指南

### 编译与烧录

```bash
# 编译
idf.py build

# 烧录并监控
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

### 按键操作

- **UP/DOWN**: 菜单导航
- **CONFIRM**: 确认/进入
- **BACK**: 返回/退出
- **LEFT/RIGHT**: 左右移动

### 创建自定义界面

```c
#include "lvgl.h"
#include "lvgl_driver.h"

void my_custom_screen(void) {
    // 创建新屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    
    // 设置白色背景（墨水屏优化）
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    // 添加 UI 元素...
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Hello World!");
    lv_obj_center(label);
    
    // 刷新到 EPD
    lvgl_display_refresh();
}
```

## 性能优化

### 墨水屏优化

1. **完整刷新**: 使用 `full_refresh = 1` 避免残影
2. **白色背景**: 所有界面使用白色背景
3. **手动刷新**: 重要变化后调用 `lvgl_display_refresh()`
4. **简化 UI**: 避免复杂渐变和动画

### 内存优化

- **显示缓冲**: 仅分配 10 行像素（`EPD_WIDTH * 10`）
- **字体**: 只启用 `montserrat_14`（减少 Flash 占用）
- **任务栈**: tick 任务 2KB，timer 任务 4KB

## 调试技巧

### 查看按键事件

按键检测日志会自动输出：
```
I (12345) LVGL_DRV: Button detected: 3
I (12350) LVGL_DRV: Key pressed: 3
```

### EPD 刷新监控

```
I (12400) LVGL_DRV: Flushing area: x1=0, y1=0, x2=799, y2=479
I (12450) LVGL_DRV: Refreshing EPD display
```

### LVGL 内存使用

添加到代码中：
```c
lv_mem_monitor_t mon;
lv_mem_monitor(&mon);
ESP_LOGI("MEM", "Used: %d, Free: %d, Frag: %d%%", 
         mon.total_size - mon.free_size, 
         mon.free_size, 
         mon.frag_pct);
```

## 已知问题

1. **刷新速度**: EPD 完整刷新需要 ~2 秒，适合静态 UI
2. **分区大小**: LVGL 增加了约 200KB 代码体积
3. **字体限制**: 仅支持 14 号字体（可在 menuconfig 启用更多）

## 下一步计划

- [ ] 集成电池信息显示
- [ ] 集成文件浏览器到 LVGL
- [ ] 添加设置界面（WiFi、BLE 配置）
- [ ] 实现部分刷新（如果 EPD 驱动支持）
- [ ] 添加更多字体大小

## 参考资料

- [LVGL 官方文档](https://docs.lvgl.io/8.3/)
- [ESP-IDF LVGL 组件](https://components.espressif.com/components/lvgl/lvgl)
- [Xteink X4 引脚定义](PIN_CONFIGURATION_SUMMARY.md)
