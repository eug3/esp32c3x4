# LVGL EPD 驱动实现方案对比

## 三种实现方案

### 方案 1: PARTIAL 模式（当前实现）

#### 配置
```c
#define DISP_BUF_LINES 5
static lv_color_t buf1[DISP_HOR_RES * DISP_BUF_LINES];  // 4.8 KB
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];  // 48 KB

lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
```

#### 特点
- ✅ **内存占用低**: ~53 KB
- ✅ **适合 ESP32-C3**: 内存足够
- ❌ **刷新控制复杂**: 耦合在 flush_cb 中
- ❌ **灵活性差**: 难以自定义刷新策略

#### 数据流
```
LVGL (分块渲染) -> buf1 (RGB565, 480×5) 
    -> flush_cb 转换 -> s_epd_framebuffer (1bpp, 800×480)
    -> 立即发送到 EPD
```

---

### 方案 2: DIRECT 模式 + RGB565（新实现）

#### 配置
```c
static lv_color_t s_lvgl_draw_buffer[DISP_HOR_RES * DISP_VER_RES];  // 750 KB
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];  // 48 KB

lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_buffers(disp, s_lvgl_draw_buffer, NULL, buf_size, 
                       LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
```

#### 特点
- ❌ **内存占用高**: ~798 KB
- ❌ **不适合 ESP32-C3**: 内存不足（只有 400 KB）
- ✅ **刷新控制简单**: 完全解耦
- ✅ **灵活性高**: 可自定义任意刷新策略
- ✅ **支持丰富的颜色**: RGB565 可以做渐变等效果

#### 数据流
```
LVGL (全屏渲染) -> s_lvgl_draw_buffer (RGB565, 480×800)
    -> flush_cb 转换 -> s_epd_framebuffer (1bpp, 800×480)
    -> 等待刷新请求 -> 发送到 EPD
```

#### 需要的硬件
- ESP32-S3 with PSRAM (8MB+)
- 或其他带大内存的 ESP32 系列

---

### 方案 3: DIRECT 模式 + 1bpp（推荐优化版）

#### 配置
```c
static uint8_t s_lvgl_draw_buffer[(DISP_HOR_RES * DISP_VER_RES) / 8];  // 48 KB
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];  // 48 KB

lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_buffers(disp, s_lvgl_draw_buffer, NULL, buf_size,
                       LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
```

#### 特点
- ✅ **内存占用适中**: ~96 KB
- ✅ **适合 ESP32-C3**: 内存足够
- ✅ **刷新控制简单**: 完全解耦
- ✅ **灵活性高**: 可自定义任意刷新策略
- ⚠️ **颜色限制**: 只支持黑白（但 EPD 本身也只有黑白）
- ✅ **转换快速**: 1bpp -> 1bpp 只需旋转映射

#### 数据流
```
LVGL (全屏渲染) -> s_lvgl_draw_buffer (1bpp, 480×800)
    -> flush_cb 旋转映射 -> s_epd_framebuffer (1bpp, 800×480)
    -> 等待刷新请求 -> 发送到 EPD
```

---

## 性能对比

| 项目 | 方案1 (PARTIAL) | 方案2 (DIRECT+RGB565) | 方案3 (DIRECT+1bpp) |
|------|----------------|---------------------|-------------------|
| **RAM占用** | ~53 KB | ~798 KB | ~96 KB |
| **适配芯片** | ESP32-C3 ✅ | ESP32-S3+PSRAM | ESP32-C3 ✅ |
| **刷新控制** | 困难 | 简单 | 简单 |
| **灵活性** | 低 | 高 | 高 |
| **颜色支持** | RGB565 | RGB565 | 1bpp |
| **转换速度** | 慢 (RGB->1bpp) | 慢 (RGB->1bpp) | 快 (1bpp->1bpp) |
| **UI 丰富度** | 高 | 高 | 低 |

---

## 使用建议

### 选择方案 1（当前实现）如果：
- ✅ 你想要最小的内存占用
- ✅ 你的刷新策略简单（不需要复杂的刷新控制）
- ✅ 你需要支持 RGB565 颜色（在 flush_cb 中转换）

### 选择方案 2（DIRECT+RGB565）如果：
- ✅ 你有足够的内存（PSRAM）
- ✅ 你需要灵活的刷新控制
- ✅ 你需要丰富的 UI 效果（渐变、图标等）
- ❌ **不推荐用于 ESP32-C3**

### 选择方案 3（DIRECT+1bpp）如果：
- ✅ 你使用 ESP32-C3（内存有限）
- ✅ 你需要灵活的刷新控制
- ✅ 你的 UI 只需要黑白（EPD 本身就是黑白）
- ✅ 你追求最快的转换速度
- ✅ **强烈推荐用于 EPD 应用**

---

## 迁移指南

### 从方案 1 迁移到方案 3

1. **修改缓冲区定义**：
```c
// 旧版本
static lv_color_t buf1[DISP_HOR_RES * DISP_BUF_LINES];

// 新版本
static uint8_t s_lvgl_draw_buffer[(DISP_HOR_RES * DISP_VER_RES) / 8];
```

2. **修改初始化代码**：
```c
// 设置颜色格式
lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

// 设置渲染模式
lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);

// 设置缓冲区
lv_display_set_buffers(disp, s_lvgl_draw_buffer, NULL,
                      sizeof(s_lvgl_draw_buffer),
                      LV_DISPLAY_RENDER_MODE_DIRECT);
```

3. **简化 flush_cb**：
```c
// 使用快速的 1bpp->1bpp 转换
copy_1bpp_with_rotation(area, px_map);
```

4. **UI 代码调整**：
```c
// 旧版本：使用颜色
lv_obj_set_style_bg_color(obj, lv_color_hex(0x123456), 0);

// 新版本：使用黑/白
lv_obj_set_style_bg_color(obj, lv_color_black(), 0);  // 或 lv_color_white()
```

---

## 内存分析

### ESP32-C3 内存限制
- **总 RAM**: ~400 KB
- **系统占用**: ~100 KB (FreeRTOS, WiFi, etc.)
- **可用 RAM**: ~300 KB

### 方案占用详情

#### 方案 1 (PARTIAL)
```
buf1:              4.8 KB
s_epd_framebuffer: 48 KB
其他:              ~10 KB
--------------------------
总计:              ~63 KB  ✅ 可用
```

#### 方案 2 (DIRECT+RGB565)
```
s_lvgl_draw_buffer: 750 KB  ❌ 超出内存！
s_epd_framebuffer:   48 KB
其他:               ~10 KB
--------------------------
总计:              ~808 KB  ❌ 不可用
```

#### 方案 3 (DIRECT+1bpp)
```
s_lvgl_draw_buffer: 48 KB
s_epd_framebuffer:  48 KB
其他:              ~10 KB
--------------------------
总计:             ~106 KB  ✅ 可用
```

---

## 结论

**对于 ESP32-C3 + EPD 4.26" 应用：**

- ❌ **不推荐** 方案 2（内存不足）
- ✅ **推荐** 方案 3（内存足够 + 刷新灵活）
- ⚠️ **备选** 方案 1（最小内存占用，但刷新控制复杂）

**方案 3 的优势**：
1. 完美适配 ESP32-C3 的内存
2. 保持 DIRECT 模式的所有优点
3. 与 EPD 1bpp 格式天然匹配
4. 转换速度快（无需 RGB->灰度转换）
5. 代码简洁易维护

**实际应用建议**：
- 使用方案 3 作为主力实现
- 保留方案 1 作为超低内存场景的备选
- 在有 PSRAM 的设备上可考虑方案 2
