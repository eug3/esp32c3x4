# 内置中文字体使用说明

## 概述

本项目已生成内置中文字体文件，无需 SD 卡即可显示中文。

## 已生成的字体

在 `main/ui/` 目录下有以下字体文件：

| 文件 | 大小 | 说明 |
|------|------|------|
| `chinese_font.bin` | 20KB | 16px, 1bpp, 约 500 常用字 |
| `chinese_font_20.bin` | 29KB | 20px, 1bpp, 约 500 常用字 |
| `chinese_font_24.bin` | 38KB | 24px, 1bpp, 约 500 常用字 |

## 重新生成字体

如需重新生成字体（例如使用不同的字体或添加更多字符）：

### 1. 安装字体生成工具

```bash
npm install -g lv_font_conv
```

### 2. 生成字体文件

```bash
# 进入工具目录
cd tools

# 生成 16px 字体
python generate_chinese_font.py /path/to/chinese_font.ttf --size 16 --bpp 1

# 生成 20px 字体
python generate_chinese_font.py /path/to/chinese_font.ttf --size 20 --bpp 1 --output chinese_font_20.bin

# 生成 24px 字体
python generate_chinese_font.py /path/to/chinese_font.ttf --size 24 --bpp 1 --output chinese_font_24.bin
```

将生成的 .bin 文件复制到 `main/ui/` 目录。

### 3. 编译测试

```bash
idf.py build
```

## 工作流程

```
SD 卡有字体          → 使用 SD 卡字体
SD 卡无字体          → 使用内置中文字体（如果有）
SD 卡无字体 + 无内置 → 使用默认英文 (montserrat)
```

## 字体加载优先级

1. SD 卡 `/sdcard/字体/` 目录下的 `.bin` 文件
2. 内置的中文字体（chinese_font.bin）
3. `lv_font_montserrat_14`（仅英文）

## 注意事项

- 内置字体会占用 Flash 空间（约 20-40KB）
- 当前包含约 500 个常用字符
- 字体越大、字符越多，文件越大
- ESP32-C3 的 Flash 足够存放常用字库
- 当前内置字体仅支持 16px 大小，其他尺寸需从 SD 卡加载
