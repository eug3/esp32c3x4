中文字体文件
============

本目录包含两种格式的字体文件：

1. **LVGL 格式** - 使用 lv_font_conv 生成
2. **XTEink 格式** - 自定义二进制格式，适合电子墨水屏

---

字体列表 - LVGL 格式
--------

1. chinese_test_16.bin (508 字节)
   - 包含字符：测试中文字体一二三ABC123
   - 用途：测试系统是否正常工作
   - 推荐用于：快速验证字体加载功能

2. chinese_16_500chars.bin (8.5 KB)
   - 包含字符：约 500 个常用中文字符
   - 字体大小：16 像素
   - 色深：1 bpp（黑白，适合电子墨水屏）
   - 推荐用于：日常使用

---

字体列表 - XTEink 格式
--------

1. HanaMinA 24 32×33.bin
   - 包含字符：所有 Unicode 字符 (0x0000 - 0xFFFF)
   - 字体大小：32×33 像素
   - 特点：全字符集支持，流式加载

使用方法 - LVGL 格式
--------

1. 将 `.bin` 文件复制到 SD 卡的 `/fonts/` 目录：

   ```
   /sdcard/
   └── fonts/
       ├── chinese_test_16.bin
       └── chinese_16_500chars.bin
   ```

2. 将 SD 卡插入 ESP32-C3 设备

3. 重启设备，系统会自动扫描并加载字体

4. 在代码中通过 font_manager 使用中文字体

使用方法 - XTEink 格式
--------

1. 将 `.bin` 文件复制到 SD 卡的 `/字体/` 目录：

   ```
   /sdcard/
   └── 字体/
       └── HanaMinA 24 32×33.bin
   ```

2. 在代码中使用 xt_eink_font_create() 加载：

   ```c
   #include "xt_eink_font.h"

   lv_font_t *font = xt_eink_font_create("/sdcard/字体/HanaMinA 24 32×33.bin");
   if (font != NULL) {
       font_manager_set_font(font);
   }
   ```

生成新字体
--------

**LVGL 格式：**
```bash
python generate_chinese_font.py <字体文件.ttf> -s 16 -o myfont.bin
```

**XTEink 格式：**
```bash
python generate_xt_eink_font.py <字体文件.ttf> -s 24 -c "要包含的字符" -o myfont.bin
```

技术细节 - LVGL 格式
--------

- 字体源：Noto Sans CJK SC（从 NotoSansCJK-Regular.ttc 提取）
- 生成工具：lv_font_conv v1.5.3
- 格式：LVGL 二进制字体格式（.bin）
- 压缩：无（--no-compress，LVGL 直接使用）

技术细节 - XTEink 格式
--------

- 文件头（20 字节）：
  - magic: 0x58454620 ("XTF ")
  - version: 1
  - width: 字体宽度
  - height: 字体高度
  - bpp: 每像素位数（1）
  - char_count: 字符数量
  - first_char: 第一个字符的 Unicode 码点
  - last_char: 最后一个字符的 Unicode 码点
  - glyph_size: 每个字形占用的字节数
  - file_size: 文件总大小

- 字形存储：
  - 每个字形占用 `ceil(width/8) × height` 字节
  - 位图按行优先存储，每行按字节对齐
  - 1-bit 色深，适合电子墨水屏

- 特点：
  - 简单直接的位图格式
  - 支持流式加载（按需读取字形）
  - 内存占用小

生成日期
--------

2026-01-04
