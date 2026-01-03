# 中文字体支持说明

## 概述

当前项目使用 LVGL 的 `.bin` 字体格式，要支持中文显示，需要生成包含中文字符的字体文件。

## 方法一：使用 lv_font_conv 生成字体文件（推荐）

### 1. 安装 lv_font_conv

```bash
npm install -g lv_font_conv
```

### 2. 生成中文字体文件

使用以下命令生成包含中文的字体文件：

```bash
lv_font_conv \
  --font /path/to/your/source-font.ttf \
  --font /path/to/source-font-bold.ttf \
  --size 16 \
  --bpp 4 \
  --format bin \
  --no-compress \
  --output /sdcard/字体/chinese_16.bin \
  --range 0x4E00-0x9FA5 \
  --range U+0020-0x007F
```

参数说明：
- `--font`: 字体文件路径（支持 TTF/OTF）
- `--size`: 字体大小（16, 20, 24 等）
- `--bpp`: 每像素位数（4 = 16 灰度，适合 E-ink）
- `--format bin`: 输出 bin 格式
- `--range 0x4E00-0x9FA5`: Unicode 汉字范围（常用汉字）
- `--range U+0020-0x007F`: 包含 ASCII 字符

### 3. 更完整的命令（推荐）

```bash
lv_font_conv \
  --font /System/Library/Fonts/PingFang.ttc \
  --size 16 \
  --bpp 4 \
  --format bin \
  --no-compress \
  --output /sdcard/字体/chinese_song_16.bin \
  --range 0x4E00-0x9FA5 \
  --range U+0020-0x007F \
  --range U+FF00-0xFFEF  # 全角字符
```

### 4. 生成不同尺寸的字体

```bash
# 12px 字体
lv_font_conv --font font.ttf --size 12 --bpp 2 --format bin --output chinese_12.bin --range 0x4E00-0x9FA5

# 16px 字体
lv_font_conv --font font.ttf --size 16 --bpp 4 --format bin --output chinese_16.bin --range 0x4E00-0x9FA5

# 20px 字体
lv_font_conv --font font.ttf --size 20 --bpp 4 --format bin --output chinese_20.bin --range 0x4E00-0x9FA5

# 24px 字体
lv_font_conv --font font.ttf --size 24 --bpp 4 --format bin --output chinese_24.bin --range 0x4E00-0x9FA5
```

### 5. 多字体混合（大小字结合）

如果字库太大，可以混合使用大小字体：

```bash
# 小字体用于正文
lv_font_conv --font font.ttf --size 14 --bpp 2 --format bin --output text_14.bin \
  --range 0x4E00-0x9FA5 --no-prefilter

# 大字体用于标题
lv_font_conv --font font.ttf --size 24 --bpp 4 --format bin --output title_24.bin \
  --range 0x4E00-0x9FA5 --no-prefilter
```

## 方法二：使用在线工具

访问 [LVGL Font Converter](https://lvgl.io/tools/fontconverter)：

1. 上传中文字体文件（TTF/OTF）
2. 设置字体大小（如 16px）
3. 选中 "Custom range" 并输入 Unicode 范围：`0x4E00-0x9FA5`
4. 点击 "Convert" 下载生成的 .bin 文件

## 部署到 SD 卡

1. 在 SD 卡根目录创建 `字体` 文件夹
2. 将生成的 `.bin` 文件放入该文件夹
3. 重启设备，字体加载器会自动扫描并加载

## SD 卡目录结构

```
/sdcard/
├── 字体/
│   ├── chinese_16.bin
│   ├── chinese_20.bin
│   └── chinese_24.bin
├── books/
│   └── *.txt
└── images/
    └── *.png
```

## 代码中使用中文字体

系统会自动扫描 SD 卡 `fonts/` 目录下的 `.bin` 文件：

```c
#include "font_manager.h"

// 获取当前字体（会自动使用加载的中文字体）
lv_font_t *font = font_manager_get_font();

// 设置指定索引的字体
font_manager_set_font_by_index(0);  // 使用第一个扫描到的字体
```

## 注意事项

1. **字库大小**：GB2312 常用汉字约 6763 个，每个字约 50-100 字节，总计约 500KB-1MB
2. **字体选择**：建议使用开源中文字体，如：
   - 思源黑体 (Source Han Sans)
   - 思源宋体 (Source Han Serif)
   - 文泉驿字体
3. **BPP 选择**：
   - 2 BPP：4 灰度，文件小，适合简单显示
   - 4 BPP：16 灰度，文件较大，显示效果更好
4. **内存限制**：ESP32-C3 内存有限，建议使用较小的字体尺寸和 BPP

## 故障排除

### 中文显示为方块（□）

1. 检查字体文件是否正确生成
2. 确认字体已部署到 SD 卡的 `fonts/` 目录
3. 检查日志中的字体扫描信息

### 字体加载失败

```bash
# 检查 SD 卡是否正确挂载
# 检查 fonts 目录是否存在
# 检查字体文件格式是否正确（bin 格式）
```

### 内存不足

- 减小字体尺寸（16px → 12px）
- 降低 BPP（4 → 2）
- 只包含常用汉字（缩小 Unicode 范围）
