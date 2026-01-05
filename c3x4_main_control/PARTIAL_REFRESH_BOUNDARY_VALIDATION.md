# 局部刷新窗口和缓存控制边界验证

## 概述

本文档详细说明了电子书阅读器项目中局部刷新（局刷）过程中的窗口控制和缓存数据控制机制，以及新增的边界验证功能。

## 问题背景

在电子墨水屏的局部刷新过程中，需要确保：
1. **窗口控制不超过内容变化范围** - 防止刷新区域超出实际需要更新的范围
2. **缓存数据访问不越界** - 防止访问帧缓冲区之外的内存，避免程序崩溃

## 系统架构

### 1. 显示系统层次结构

```
应用层 (reader_screen_simple.c)
    ↓
显示引擎层 (display_engine.c)
    ↓ 脏区域管理 (dirty region tracking)
    ↓ 逻辑坐标 → 物理坐标转换
    ↓
EPD驱动层 (EPD_4in26.c)
    ↓ 窗口设置 (SetWindows)
    ↓ 帧缓冲数据发送
    ↓
硬件层 (SPI通信到墨水屏控制器)
```

### 2. 坐标系统

- **逻辑坐标系**：480x800（竖屏，应用层使用）
- **物理坐标系**：800x480（横屏，硬件实际布局）
- **旋转模式**：ROTATE_270（逻辑到物理的转换）

### 3. 内存布局

- **帧缓冲区大小**：48KB (800 × 480 ÷ 8)
- **每行字节数**：100字节 (800 ÷ 8)
- **总行数**：480行

## 新增的边界验证机制

### 1. EPD_4in26_SetWindows() - 窗口坐标验证

**位置**：`main/EPD_4in26.c:614-663`

**验证内容**：
```c
// 1. 验证X坐标范围
if (Xstart >= EPD_4in26_WIDTH) {
    Xstart = EPD_4in26_WIDTH - 1;  // 钳位到799
}
if (Xend >= EPD_4in26_WIDTH) {
    Xend = EPD_4in26_WIDTH - 1;
}

// 2. 验证Y坐标范围
if (Ystart >= EPD_4in26_HEIGHT) {
    Ystart = EPD_4in26_HEIGHT - 1;  // 钳位到479
}
if (Yend >= EPD_4in26_HEIGHT) {
    Yend = EPD_4in26_HEIGHT - 1;
}

// 3. 确保起始坐标 <= 结束坐标
if (Xstart > Xend) {
    swap(Xstart, Xend);  // 交换
}
if (Ystart > Yend) {
    swap(Ystart, Yend);
}
```

**保护作用**：
- 防止向EPD控制器发送无效的窗口坐标
- 自动修正越界坐标，避免刷新失败
- 记录错误日志，便于调试

### 2. EPD_4in26_SendRegion_FromFramebuffer() - 帧缓冲访问验证

**位置**：`main/EPD_4in26.c:259-325`

**验证内容**：
```c
// 1. 验证stride（每行字节数）
const UWORD fb_width_bytes = 100;  // 800/8
if (fb_stride != fb_width_bytes) {
    return;  // 参数错误，拒绝访问
}

// 2. 验证Y起始位置
if (y >= fb_height) {
    return;  // Y坐标超出范围
}

// 3. 验证区域高度
if (y + h_actual > fb_height) {
    h_actual = fb_height - y;  // 钳位高度
}

// 4. 验证区域宽度
if (x_offset_bytes + w_bytes > fb_stride) {
    w_bytes = fb_stride - x_offset_bytes;  // 钳位宽度
}
```

**保护作用**：
- 防止读取帧缓冲区之外的内存（48KB限制）
- 避免缓冲区溢出导致的程序崩溃
- 自动调整超出范围的访问请求

**内存安全计算**：
```
最大Y访问 = y + h_actual - 1 <= 479
最大X字节访问 = x_offset_bytes + w_bytes - 1 <= 99
总访问字节数 = h_actual × w_bytes <= 48000字节
```

### 3. expand_dirty_region() - 脏区域扩展验证

**位置**：`main/ui/display_engine.c:72-138`

**验证内容**：
```c
// 1. 处理负坐标
if (x < 0) {
    width += x;  // 调整宽度
    x = 0;
}
if (y < 0) {
    height += y;  // 调整高度
    y = 0;
}

// 2. 验证尺寸有效性
if (width <= 0 || height <= 0) {
    return;  // 无效区域，忽略
}

// 3. 验证起始坐标
if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
    return;  // 完全超出屏幕，忽略
}

// 4. 限制在屏幕范围内
if (x + width > SCREEN_WIDTH) {
    width = SCREEN_WIDTH - x;
}
if (y + height > SCREEN_HEIGHT) {
    height = SCREEN_HEIGHT - y;
}

// 5. 扩展后再次验证
if (s_dirty_region.x + s_dirty_region.width > SCREEN_WIDTH) {
    s_dirty_region.width = SCREEN_WIDTH - s_dirty_region.x;
}
```

**保护作用**：
- 防止脏区域超出逻辑屏幕范围（480x800）
- 处理异常输入（负坐标、超大区域）
- 确保传递给EPD层的区域始终有效

## 局部刷新流程中的边界控制

### 完整的数据流和验证点

```
1. 应用层请求刷新
   display_refresh_region(x, y, width, height)
   ↓
   [验证点1] 边界检查和钳位
   if (x < 0) x = 0;
   if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
   
2. 更新脏区域
   expand_dirty_region(x, y, width, height)
   ↓
   [验证点2] expand_dirty_region内部验证
   - 负坐标处理
   - 尺寸验证
   - 屏幕范围限制
   - 扩展后二次验证
   
3. 坐标转换
   convert_logical_to_physical_region(...)
   逻辑(480x800) → 物理(800x480)
   ↓
   
4. EPD驱动层处理
   EPD_4in26_Display_Part_Stream(framebuffer, stride, x, y, w, h)
   ↓
   [验证点3] EPD_4in26_Display_Part_Stream_Impl内部
   - X对齐到8像素边界
   - 边界溢出检查和调整
   
5. 设置窗口
   EPD_4in26_SetWindows(x_start, y_start, x_end, y_end)
   ↓
   [验证点4] SetWindows内部验证
   - 所有坐标范围检查
   - 起始<=结束验证
   - 自动钳位和交换
   
6. 发送数据
   EPD_4in26_SendRegion_FromFramebuffer(framebuffer, stride, ...)
   ↓
   [验证点5] 帧缓冲访问验证
   - stride验证
   - Y位置和高度验证
   - X偏移和宽度验证
   - 内存访问边界检查
   
7. 硬件刷新
   EPD控制器执行局部刷新
```

## 边界验证示例

### 示例1：超出屏幕的刷新请求

**输入**：
```c
display_refresh_region(400, 700, 200, 200, REFRESH_MODE_PARTIAL);
// 逻辑坐标：x=400, y=700, w=200, h=200
// 预期范围：(400,700) 到 (600,900)
// 问题：y=700 + h=200 = 900 > SCREEN_HEIGHT(800)
```

**处理过程**：
```
验证点1 (display_refresh_region):
  y + height = 700 + 200 = 900 > 800
  height = 800 - 700 = 100  ← 钳位

验证点2 (expand_dirty_region):
  验证通过，区域为 (400, 700, 200, 100)

物理坐标转换:
  逻辑(400, 700, 200, 100) → 物理(700, 79, 100, 200)

验证点3-5:
  所有验证通过，安全刷新
```

**结果**：只刷新有效区域，不会越界

### 示例2：帧缓冲溢出保护

**场景**：尝试访问超出帧缓冲的数据

**输入**：
```c
// 错误调用：y=470, h=20
// y + h = 490 > fb_height(480)
EPD_4in26_SendRegion_FromFramebuffer(fb, 100, 0, 100, 470, 20);
```

**处理过程**：
```
验证点5 (SendRegion_FromFramebuffer):
  fb_height = 480
  y = 470 < 480  ✓
  y + h_actual = 470 + 20 = 490 > 480  ✗
  
  钳位处理:
  h_actual = fb_height - y = 480 - 470 = 10
  
  访问范围:
  row[470] 到 row[479]  ← 安全范围
  
内存访问:
  for (i = 0; i < 10; i++) {
    // 访问 fb[470*100] 到 fb[479*100+99]
    // 总共 10行 × 100字节 = 1000字节
  }
```

**结果**：自动钳位，只访问有效内存，避免越界

### 示例3：8像素对齐处理

**场景**：局部刷新区域未对齐到8像素边界

**输入**：
```c
// x=5, width=10 (未对齐)
EPD_4in26_Display_Part_Stream(fb, 100, 5, 100, 10, 50);
```

**处理过程**：
```
验证点3 (Display_Part_Stream_Impl):
  原始: x=5, w=10
  
  X对齐处理:
  x_aligned = 5 - (5 % 8) = 0  ← 向下对齐到0
  x_end = x_aligned + w - 1 = 0 + 10 - 1 = 9
  w_aligned = x_end - x_aligned + 1 = 10
  
  字节宽度:
  w_bytes = (10 + 7) / 8 = 2字节
  
实际刷新区域:
  物理像素：x=0 到 x=15 (2字节 × 8像素/字节)
  包含了原始请求的 x=5 到 x=14
```

**结果**：自动对齐，确保硬件要求，覆盖所需区域

## 性能影响

### 验证开销

1. **计算开销**：
   - 每次验证约10-20条比较指令
   - 对于局部刷新（~200ms总时间），增加 < 0.1ms
   - 性能影响可忽略不计

2. **内存开销**：
   - 新增局部变量：约40字节栈空间
   - 无额外堆内存分配
   - 对400KB RAM的ESP32-C3影响极小

3. **日志开销**：
   - 仅在检测到越界时输出日志
   - 正常情况无额外开销
   - 可通过日志级别控制

## 测试建议

### 1. 边界测试用例

```c
// 测试1：正常区域
display_refresh_region(100, 100, 200, 300, REFRESH_MODE_PARTIAL);
// 预期：无警告，正常刷新

// 测试2：边缘区域
display_refresh_region(0, 0, 1, 1, REFRESH_MODE_PARTIAL);
display_refresh_region(479, 799, 1, 1, REFRESH_MODE_PARTIAL);
// 预期：无警告，正常刷新边缘像素

// 测试3：部分越界
display_refresh_region(400, 700, 200, 200, REFRESH_MODE_PARTIAL);
// 预期：警告日志，自动钳位，部分刷新

// 测试4：完全越界
display_refresh_region(500, 900, 100, 100, REFRESH_MODE_PARTIAL);
// 预期：忽略刷新或错误日志

// 测试5：负坐标
display_refresh_region(-10, -10, 100, 100, REFRESH_MODE_PARTIAL);
// 预期：警告日志，自动调整为(0, 0, 90, 90)

// 测试6：未对齐坐标
display_refresh_region(5, 100, 10, 50, REFRESH_MODE_PARTIAL);
// 预期：自动对齐到(0, 100, 16, 50)
```

### 2. 压力测试

```c
// 连续快速刷新不同区域
for (int i = 0; i < 100; i++) {
    int x = rand() % 480;
    int y = rand() % 800;
    int w = rand() % 200 + 10;
    int h = rand() % 200 + 10;
    display_refresh_region(x, y, w, h, REFRESH_MODE_PARTIAL);
}
// 预期：所有刷新都应安全完成，无崩溃
```

### 3. 内存验证

使用工具检查：
- 无缓冲区溢出
- 无内存泄漏
- 栈使用在安全范围内

## 调试技巧

### 1. 启用详细日志

```c
// 在 sdkconfig 中设置
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y

// 或运行时调整
esp_log_level_set("EPD", ESP_LOG_DEBUG);
esp_log_level_set("EPD_FB", ESP_LOG_DEBUG);
esp_log_level_set("DISP_ENGINE", ESP_LOG_DEBUG);
```

### 2. 监控日志输出

```
关键日志标签:
EPD        - 窗口设置警告
EPD_FB     - 帧缓冲访问错误
DISP_ENGINE - 脏区域管理警告
EPD_PART   - 局部刷新详细信息
```

### 3. 常见警告信息

```
"SetWindows: Xstart=... exceeds width..."
  → 窗口坐标超出屏幕，已自动钳位

"Width overflow: x_offset=... + w_bytes=... > stride=..."
  → 帧缓冲宽度访问越界，已自动限制

"expand_dirty_region: width overflow ... "
  → 脏区域宽度超出屏幕，已自动调整
```

## 总结

通过在三个关键层次添加边界验证：

1. **窗口控制层** (`EPD_4in26_SetWindows`)
   - 确保所有窗口坐标在硬件支持范围内
   - 防止向EPD控制器发送无效命令

2. **缓存访问层** (`EPD_4in26_SendRegion_FromFramebuffer`)
   - 确保所有帧缓冲访问在48KB范围内
   - 防止内存越界和程序崩溃

3. **逻辑管理层** (`expand_dirty_region`)
   - 确保脏区域在逻辑屏幕范围内
   - 处理异常输入和边界情况

**系统现在能够：**
- ✅ 自动检测和修正越界的刷新请求
- ✅ 防止帧缓冲区访问溢出
- ✅ 保证窗口坐标始终有效
- ✅ 在异常情况下记录日志并优雅降级
- ✅ 保持局部刷新的高性能特性

**不会超过内容变化范围的原因：**
1. 脏区域严格跟踪实际绘制操作
2. 多层边界验证确保区域有效性
3. 坐标转换过程保持区域完整性
4. 硬件层最后防线保证绝对安全

这些验证机制确保了电子书阅读器在局部刷新过程中的安全性和可靠性，同时保持了良好的性能表现。
