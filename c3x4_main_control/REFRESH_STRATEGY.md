# EPD 刷新策略统一说明

## 刷新模式分类

系统支持三种 EPD 刷新模式：

1. **EPD_REFRESH_FULL** - 全刷模式
   - 特点：最清晰，完全消除鬼影
   - 速度：最慢（约 2 秒）
   - 使用场景：组件间切换（屏幕切换）

2. **EPD_REFRESH_FAST** - 快刷模式
   - 特点：全屏快速刷新，平衡速度和清晰度
   - 速度：中等（约 1.5 秒）
   - 使用场景：首次全刷后的默认模式

3. **EPD_REFRESH_PARTIAL** - 局刷模式
   - 特点：只刷新变化区域，最快
   - 速度：最快（0.3-0.5 秒）
   - 使用场景：组件内操作（焦点切换、列表滚动等）

## 统一刷新策略

### 1. 组件间切换（屏幕切换）

**强制使用全刷模式**，确保屏幕完全清晰无鬼影。

实现位置：`screen_manager.c`

```c
void screen_manager_show_xxx(void)
{
    // 重置刷新状态
    lvgl_reset_refresh_state();
    
    // 强制全刷
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);
    
    // 创建并显示新屏幕
    xxx_screen_create(...);
}
```

### 2. 组件内操作（同一屏幕内的交互）

**使用局刷模式**，提升响应速度。

#### 示例1：焦点切换（index_screen.c）

```c
static void index_button_focus_event_cb(lv_event_t *e)
{
    // ... 更新 UI ...
    
    // 触发渲染
    lvgl_trigger_render(NULL);
    
    // 使用局刷
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_display_refresh();
}
```

#### 示例2：目录导航（file_browser.c）

```c
if (read_directory(new_path)) {
    update_file_list_display();
    lvgl_trigger_render(NULL);
    
    // 使用局刷
    lvgl_set_refresh_mode(EPD_REFRESH_PARTIAL);
    lvgl_display_refresh();
}
```

### 3. 屏幕初始显示

屏幕首次创建时，**不设置刷新模式**，使用 screen_manager 预设的模式（FULL）。

```c
void xxx_screen_create(...)
{
    // 创建 UI 组件
    lv_obj_t *screen = lv_obj_create(NULL);
    // ... 创建其他组件 ...
    
    // 触发渲染
    for (int i = 0; i < 5; i++) {
        lvgl_trigger_render(NULL);
    }
    
    // 刷新 - 使用 screen_manager 预设的模式
    lvgl_display_refresh();
}
```

## 刷新流程详解

### 完整刷新流程

1. **LVGL 渲染阶段**
   - 调用 `lvgl_trigger_render(NULL)` 触发 LVGL 渲染
   - LVGL 计算脏区域，调用 `disp_flush_cb()` 多次
   - `disp_flush_cb()` 将像素写入完整 framebuffer (`s_epd_framebuffer`)
   - 记录所有脏区域的并集到 `s_dirty_area`

2. **EPD 刷新阶段**
   - 调用 `lvgl_display_refresh()` 发起异步刷新请求
   - 刷新任务根据模式选择：
     - **FULL**: 发送完整 framebuffer 到 EPD
     - **FAST**: 发送完整 framebuffer 到 EPD（快速模式）
     - **PARTIAL**: 从完整 framebuffer 裁剪脏区域，只发送裁剪部分

### 局刷关键修复

**问题**：之前传递偏移后的指针给 `EPD_4in26_Display_Partial()`，导致数据错位。

**解决方案**：传递完整 framebuffer 起始地址，让 EPD 函数自己裁剪。

修改前：
```c
const uint32_t fb_offset = epd_y * (EPD_WIDTH / 8) + (epd_x / 8);
uint8_t *partial_data = &s_epd_framebuffer[fb_offset];  // ❌ 错误
EPD_4in26_Display_Partial(partial_data, epd_x, epd_y, epd_w, epd_h);
```

修改后：
```c
// ✅ 正确：传递完整 framebuffer
EPD_4in26_Display_Partial(s_epd_framebuffer, epd_x, epd_y, epd_w, epd_h);
```

`EPD_4in26_Display_Partial()` 内部会正确计算偏移：
```c
// 从完整 framebuffer 裁剪数据
for(i = 0; i < h; i++) {
    UBYTE *row_ptr = Image + (y + i) * full_width_bytes + x_byte;
    EPD_4in26_SendData2(row_ptr, window_width_bytes);
}
```

## 坐标转换说明

### LVGL 逻辑坐标系 (480x800)
- 竖屏布局，供应用层使用
- 原点在左上角

### EPD 物理坐标系 (800x480)
- 横屏布局，硬件实际分辨率
- 使用 ROTATE_270 映射

### 转换公式
```c
// LVGL(x,y) -> EPD(memX,memY)
memX = y
memY = EPD_HEIGHT - 1 - x  // 480 - 1 - x
```

### 局刷坐标对齐
EPD 硬件要求 X 坐标必须是 8 的倍数（字节对齐）：
```c
if (epd_x % 8 != 0) {
    const int32_t orig_x = epd_x;
    epd_x = (epd_x / 8) * 8;      // 向下对齐
    epd_w += (orig_x - epd_x);     // 补偿宽度
}
if (epd_w % 8 != 0) {
    epd_w = ((epd_w + 7) / 8) * 8; // 向上对齐
}
```

## 使用指南

### 添加新屏幕

1. 在 `screen_manager.c` 添加导航函数：
```c
void screen_manager_show_new_screen(void)
{
    lvgl_reset_refresh_state();
    lvgl_set_refresh_mode(EPD_REFRESH_FULL);  // 组件间切换用全刷
    new_screen_create(g_context->indev);
}
```

2. 在新屏幕中：
   - 组件内操作使用 `EPD_REFRESH_PARTIAL`
   - 首次显示不设置模式，继承 screen_manager 的 FULL

### 性能优化建议

1. **减少不必要的刷新**
   - 合并多个小更新为一次刷新
   - 使用防抖避免频繁刷新

2. **局刷计数器**
   - 系统自动在 10 次局刷后执行一次全刷
   - 用于消除累积的鬼影

3. **刷新模式选择**
   - 大范围变化：用 FULL 或 FAST
   - 小范围变化：用 PARTIAL
   - 切换屏幕：强制 FULL

## 关键 API

### lvgl_driver.h 提供的接口

```c
// 刷新函数
void lvgl_display_refresh(void);         // 使用当前模式刷新
void lvgl_display_refresh_full(void);    // 强制全刷
void lvgl_display_refresh_fast(void);    // 强制快刷
void lvgl_display_refresh_partial(void); // 强制局刷

// 模式控制
void lvgl_set_refresh_mode(epd_refresh_mode_t mode);
epd_refresh_mode_t lvgl_get_refresh_mode(void);

// 状态管理
void lvgl_reset_refresh_state(void);     // 重置脏区域和计数器
bool lvgl_is_refreshing(void);           // 检查是否正在刷新

// 手动渲染（DIRECT 模式必需）
void lvgl_trigger_render(lv_display_t *disp);
```

## 调试技巧

### 查看刷新日志

系统会输出详细的刷新日志：

```
I (12345) LVGL_DRV: EPD refresh task: PARTIAL #1/10 LVGL(100,200,80x60) -> EPD(x=200,y=280,60x80)
```

解读：
- `PARTIAL #1/10`: 第1次局刷，累计10次后会全刷
- `LVGL(100,200,80x60)`: LVGL 坐标系的脏区域
- `EPD(x=200,y=280,60x80)`: 转换到 EPD 坐标系后的区域

### 常见问题

1. **局刷显示错误**
   - 检查是否传递了完整 framebuffer
   - 验证坐标转换是否正确
   - 确认已调用 `lvgl_trigger_render()`

2. **刷新太慢**
   - 检查是否误用了 FULL 模式
   - 考虑使用 PARTIAL 优化组件内操作

3. **鬼影累积**
   - 系统会自动每 10 次局刷执行一次全刷
   - 屏幕切换会强制全刷清除鬼影

## 版本历史

- **2026-01-03**: 统一刷新策略，修复局刷数据错位问题
  - 组件间切换强制全刷
  - 组件内操作使用局刷
  - 修复局刷 framebuffer 裁剪错误
