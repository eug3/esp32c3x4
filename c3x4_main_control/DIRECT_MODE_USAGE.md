# LVGL DIRECT 模式 EPD 刷新使用指南

## 概述

新的实现使用 LVGL 的 `LV_DISPLAY_RENDER_MODE_DIRECT` 模式，实现了 LVGL 渲染和 EPD 刷新的完全解耦。

## 工作原理

### 1. 双缓冲架构

```
LVGL 渲染 -> s_lvgl_draw_buffer (RGB565, 480x800)
              ↓ (flush_cb 转换)
           s_epd_framebuffer (1bpp, 800x480)
              ↓ (刷新任务发送)
           EPD 硬件
```

### 2. 数据流

1. **LVGL 渲染阶段**：
   - LVGL 直接渲染到全屏大小的 `s_lvgl_draw_buffer`
   - 使用 RGB565 格式
   - 只渲染变化的区域（DIRECT 模式的优化）

2. **Flush Callback**：
   - 将 RGB565 转换为 1bpp
   - 应用 ROTATE_270 坐标映射
   - 写入 `s_epd_framebuffer`
   - **不触发** EPD 硬件刷新

3. **EPD 刷新任务**：
   - 等待刷新请求
   - 根据刷新模式处理 `s_epd_framebuffer`
   - 发送数据到 EPD 硬件

## 使用方法

### 基本刷新

```c
// 1. 修改 UI（LVGL 会自动标记脏区）
lv_label_set_text(label, "New Text");

// 2. 触发渲染（将变化写入 framebuffer）
lvgl_trigger_render(NULL);

// 3. 触发 EPD 刷新
lvgl_display_refresh_fast();  // 快刷
// 或
lvgl_display_refresh_partial();  // 局刷
// 或
lvgl_display_refresh_full();  // 全刷
```

### 不同刷新模式

#### 1. 快刷 (FAST)
```c
// 适用场景：频繁更新，可接受轻微鬼影
lvgl_display_refresh_fast();
```
- 发送整个 `s_epd_framebuffer` 到 EPD
- 使用快速刷新模式
- 速度最快（~1-2秒）
- 可能有轻微鬼影

#### 2. 局部刷新 (PARTIAL)
```c
// 适用场景：小区域更新，追求最快响应
lvgl_display_refresh_partial();
```
- 只发送脏区域到 EPD
- 自动跟踪变化区域
- 速度快（取决于区域大小）
- 多次局刷后自动触发快刷（防止鬼影累积）

#### 3. 全刷 (FULL)
```c
// 适用场景：屏幕切换，需要最清晰显示
lvgl_display_refresh_full();
```
- 发送整个 `s_epd_framebuffer` 到 EPD
- 使用完整刷新模式（有闪烁）
- 速度慢（~5-10秒）
- 显示最清晰，无鬼影

### 刷新策略建议

```c
// 示例：智能刷新策略
void smart_refresh(ui_update_type_t type) {
    // 修改 UI
    update_ui(type);
    
    // 触发渲染
    lvgl_trigger_render(NULL);
    
    // 根据更新类型选择刷新模式
    switch (type) {
        case UI_UPDATE_MINOR:  // 小更新（如时间）
            lvgl_display_refresh_partial();
            break;
            
        case UI_UPDATE_MAJOR:  // 大更新（如数据刷新）
            lvgl_display_refresh_fast();
            break;
            
        case UI_UPDATE_SCREEN:  // 屏幕切换
            lvgl_display_refresh_full();
            break;
    }
}
```

## 高级用法

### 1. 批量更新

```c
// 多个 UI 元素更新后一次性刷新
lv_label_set_text(label1, "Text 1");
lv_label_set_text(label2, "Text 2");
lv_bar_set_value(bar, 50, LV_ANIM_OFF);

// 一次性渲染和刷新
lvgl_trigger_render(NULL);
lvgl_display_refresh_fast();
```

### 2. 刷新完成回调

```c
void on_refresh_complete(void) {
    ESP_LOGI(TAG, "EPD 刷新完成，可以进行下一步操作");
    // 恢复用户输入
    // 更新状态指示器
}

// 注册回调
lvgl_register_refresh_complete_callback(on_refresh_complete);
```

### 3. 检查刷新状态

```c
// 避免在刷新期间更新 UI
if (!is_epd_refreshing()) {
    // 安全地更新 UI
    update_ui();
    lvgl_trigger_render(NULL);
    lvgl_display_refresh_fast();
}
```

## 性能优化

### 1. 减少不必要的刷新

```c
// ❌ 不好：每次小改动都刷新
void bad_update(int value) {
    lv_label_set_text_fmt(label, "%d", value);
    lvgl_trigger_render(NULL);
    lvgl_display_refresh_fast();  // 太频繁！
}

// ✅ 好：批量更新后刷新
void good_update(void) {
    for (int i = 0; i < 10; i++) {
        lv_label_set_text_fmt(labels[i], "%d", values[i]);
    }
    lvgl_trigger_render(NULL);
    lvgl_display_refresh_fast();  // 一次性刷新
}
```

### 2. 使用定时刷新

```c
// 每秒刷新一次（而不是每次数据更新都刷新）
static void refresh_timer_cb(lv_timer_t *timer) {
    if (ui_has_changes()) {
        lvgl_trigger_render(NULL);
        lvgl_display_refresh_partial();
    }
}

// 创建定时器
lv_timer_create(refresh_timer_cb, 1000, NULL);
```

### 3. 局刷计数管理

```c
// 系统会自动管理局刷计数：
// - 每次 PARTIAL 刷新后计数 +1
// - 达到 10 次后自动重置
// - 重置后下次会执行快刷清除鬼影

// 你也可以手动强制全刷：
if (need_clear_ghosting) {
    lvgl_display_refresh_full();  // 重置计数并全刷
}
```

## 内存使用

- `s_lvgl_draw_buffer`: 480 × 800 × 2 = 750 KB (RGB565)
- `s_epd_framebuffer`: 800 × 480 ÷ 8 = 48 KB (1bpp)
- **总计**: ~798 KB

注意：这比之前的实现使用更多内存，但换来了：
- 完全解耦的刷新控制
- 更清晰的数据流
- 更好的可维护性

## 调试

### 查看刷新日志

```
I (12345) LVGL_DRV: disp_flush_cb #1: area(0,0)-(479,799), cf=11
I (12346) LVGL_DRV: [DIRTY] init: (0,0)-(479,799)
I (12347) LVGL_DRV: EPD refresh task: received request, mode=1
I (12348) LVGL_DRV: EPD refresh task: FAST refresh
I (14000) LVGL_DRV: EPD refresh task: complete
```

### 常见问题

1. **刷新太慢**
   - 检查是否使用了 FULL 模式
   - 考虑改用 FAST 或 PARTIAL

2. **鬼影累积**
   - 局刷超过 10 次会自动快刷
   - 可以手动调用 `lvgl_display_refresh_full()`

3. **UI 更新不显示**
   - 确保调用了 `lvgl_trigger_render()`
   - 确保调用了刷新函数

## 与旧版本的区别

| 特性 | 旧版本 (PARTIAL 模式) | 新版本 (DIRECT 模式) |
|------|---------------------|-------------------|
| 缓冲区大小 | 480×5×2 = 4.8 KB | 480×800×2 = 750 KB |
| 渲染方式 | 分块渲染 | 全屏渲染 |
| 刷新控制 | 耦合在 flush_cb | 完全独立 |
| 刷新时机 | 自动 | 手动控制 |
| 灵活性 | 低 | 高 |
| 内存占用 | 低 | 高 |

## 总结

新的 DIRECT 模式实现提供了：

✅ **完全控制**：手动控制何时以及如何刷新 EPD  
✅ **灵活刷新**：根据场景选择最优刷新策略  
✅ **数据一致**：framebuffer 始终保持最新状态  
✅ **易于调试**：清晰的数据流和日志  

权衡：
⚠️ **内存增加**：需要 ~750 KB RAM（ESP32-C3 通常有 400 KB RAM）

**注意**：ESP32-C3 可能没有足够的 RAM 来支持全屏 RGB565 缓冲区。如果遇到内存不足，可能需要：
1. 使用外部 PSRAM
2. 或者回退到 PARTIAL 模式
3. 或者使用较低的颜色深度（如 RGB332）
