# 手绘UI系统实现说明

## 概述

已成功将系统从LVGL迁移到基于GUI_Paint的手工绘图系统，完全移除了LVGL依赖。

## 系统架构

### 显示引擎 (display_engine.c/h)

**核心配置：**
- 逻辑屏幕尺寸：480x800（竖屏）
- 物理屏幕尺寸：800x480（横屏）
- 旋转模式：ROTATE_270（逻辑竖屏 → 物理横屏）
- 颜色模式：1bpp (黑白)
- 帧缓冲区：48KB

**主要功能：**
- `display_engine_init()` - 初始化显示引擎
- `display_clear(color)` - 清空屏幕
- `display_draw_text()` - 绘制英文文本
- `display_draw_rect()` - 绘制矩形
- `display_refresh()` - 刷新显示（支持全刷/快刷/局刷）

### 屏幕管理器 (screen_manager.c/h)

管理多个屏幕的切换和导航：
- `screen_manager_init()` - 初始化管理器
- `screen_manager_register()` - 注册屏幕
- `screen_manager_show()` - 显示指定屏幕
- `screen_manager_back()` - 返回上一屏幕
- `screen_manager_draw()` - 绘制当前屏幕

### 已实现的屏幕

#### 1. 首页 (home_screen.c)
- 显示标题 "Xteink X4 eReader"
- 显示电池电量
- 菜单项：
  - Files（文件浏览器）
  - Settings（设置）
- 支持上下导航和确认选择

#### 2. 文件浏览器 (file_browser_screen.c)
- 基础框架已实现
- 待完善：文件列表显示和SD卡集成

#### 3. 阅读器 (reader_screen_simple.c)
- 基础框架已实现
- 待完善：EPUB/TXT解析和翻页功能

#### 4. 设置界面 (settings_screen_simple.c)
- 基础框架已实现
- 待完善：设置项列表和配置保存

#### 5. 图片查看器 (image_viewer_screen.c)
- 基础框架已实现
- 待完善：图片解码和显示

## 内存使用

**Flash代码：** ~456KB  
**DRAM：** ~154KB (47.92%)
- .text: 82KB
- .bss: 60KB (主要是帧缓冲区48KB)
- .data: 11KB

**总镜像大小：** ~654KB

**内存优势：**
- 相比LVGL版本节省了约100KB RAM
- 不需要LVGL的绘图缓冲区和对象管理开销

## 后续可添加的功能

### 1. 文件浏览器增强
```c
// 需要实现的功能
- SD卡目录扫描
- 文件列表滚动显示
- 文件类型图标
- 文件排序（按名称/时间/大小）
- 支持的格式：EPUB, TXT, JPG, PNG
```

### 2. EPUB阅读器
```c
// 需要集成
- EPUB解压和解析
- HTML/CSS渲染（简化版）
- 翻页动画
- 书签管理
- 阅读进度保存
- 字体大小调节
```

### 3. 中文字体支持
```c
// 当前使用Font12，需要添加
- 中文字体库（GB2312或UTF-8）
- display_draw_text_cn() 实现
- 字体缓存优化
```

### 4. 设置界面
```c
// 设置项
- 屏幕亮度（如果硬件支持）
- 自动休眠时间
- 刷新模式偏好（快刷/全刷）
- WiFi配置
- 系统信息显示
```

### 5. 图片查看器
```c
// 需要实现
- JPEG解码（使用esp-jpeg）
- PNG解码（使用upng）
- 图片缩放和平移
- 幻灯片模式
```

### 6. 电池管理优化
```c
// 增强功能
- 低电量警告
- 充电状态动画
- 电池图标显示
- 省电模式
```

### 7. WiFi功能
```c
// 网络功能
- WiFi AP配置界面
- 文件传输（FTP/HTTP）
- 在线书库下载
- 固件OTA升级
```

## 绘图API使用示例

### 基本绘图
```c
// 清屏
display_clear(COLOR_WHITE);

// 绘制文本
display_draw_text(x, y, "Hello", COLOR_BLACK, COLOR_WHITE);

// 绘制矩形
display_draw_rect(x, y, width, height, COLOR_BLACK, false);  // 空心
display_draw_rect(x, y, width, height, COLOR_BLACK, true);   // 实心

// 刷新屏幕
display_refresh(REFRESH_MODE_FULL);      // 全刷
display_refresh(REFRESH_MODE_FAST);      // 快刷
display_refresh(REFRESH_MODE_PARTIAL);   // 局刷
```

### GUI_Paint底层API
```c
// 如果需要更多绘图功能，可直接使用GUI_Paint
#include "GUI_Paint.h"

Paint_DrawLine(x1, y1, x2, y2, COLOR_BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
Paint_DrawCircle(cx, cy, radius, COLOR_BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
Paint_DrawString_EN(x, y, "Text", &Font20, COLOR_BLACK, COLOR_WHITE);
```

## 性能优化建议

1. **局部刷新优化**
   - 仅在必要时使用全刷
   - 小区域更新使用局刷
   - 快速交互使用快刷

2. **内存优化**
   - 大文件分块读取
   - 图片按需解码
   - 使用静态缓冲区避免动态分配

3. **响应速度**
   - 按钮去抖动已实现
   - 可添加屏幕切换动画（简单的淡入淡出）

## 调试技巧

```c
// 在display_engine.c中可添加帧缓冲区dump
void display_dump_framebuffer(const char *filename) {
    // 保存帧缓冲区到SD卡用于调试
}

// 添加性能计时
uint32_t start = esp_timer_get_time();
display_refresh(REFRESH_MODE_FULL);
ESP_LOGI(TAG, "Refresh took %u ms", (esp_timer_get_time() - start) / 1000);
```

## 已知问题和限制

1. **字体限制**
   - 当前仅支持ASCII字符（Font12, Font16, Font20, Font24）
   - 中文显示需要添加字体库

2. **图形能力**
   - 仅支持1bpp黑白显示
   - 不支持灰度（4bpp需要192KB缓冲区）

3. **刷新速度**
   - 全刷约2秒
   - 快刷约1.5秒
   - 局刷约0.3秒（可能有残影）

## 版本历史

- **v1.0** (2026-01-04)
  - 完成LVGL到手工绘图的迁移
  - 实现基础屏幕框架
  - 支持按键导航和屏幕切换
  - 内存优化到48KB帧缓冲区
