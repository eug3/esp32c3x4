中文字体文件
============

已生成的 LVGL 格式中文字体文件，可直接复制到 SD 卡使用。

字体列表
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

使用方法
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

技术细节
--------

- 字体源：Noto Sans CJK SC（从 NotoSansCJK-Regular.ttc 提取）
- 生成工具：lv_font_conv v1.5.3
- 格式：LVGL 二进制字体格式（.bin）
- 压缩：无（--no-compress，LVGL 直接使用）

生成日期
--------
2026-01-03
