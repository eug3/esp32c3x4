# 中文字体文件使用说明

## 文件列表

将以下 `.bin` 字体文件复制到 SD 卡的 `/sdcard/字体/` 目录：

| 文件名 | 大小 | 字体大小 | 用途 |
|--------|------|----------|------|
| `GenJyuuGothic-Monospace-Light-14.bin` | 27 KB | 14px | 小号字体，适合显示更多信息 |
| `GenJyuuGothic-Monospace-Light-16.bin` | 32 KB | 16px | 标准字体，推荐使用 |
| `GenJyuuGothic-Monospace-Light-18.bin` | 37 KB | 18px | 中号字体，阅读舒适 |
| `GenJyuuGothic-Monospace-Light-20.bin` | 43 KB | 20px | 大号字体，适合大屏阅读 |

## 字体信息

- **字体**: GenJyuuGothic-Monospace-Light（源真黑体等宽细体）
- **字符集**: 491 个常用汉字
- **颜色深度**: 4 bpp（16 级灰度）
- **格式**: LVGL 二进制格式

## 复制到 SD 卡

```bash
# 在 macOS/Linux 上
mkdir -p /Volumes/SD卡/字体
cp GenJyuuGothic-Monospace-Light-*.bin /Volumes/SD卡/字体/

# 或使用文件管理器直接复制
```

## 在设备上使用

1. 将 SD 卡插入设备
2. 在设置界面选择字体
3. 字体加载器会自动扫描 `/sdcard/字体/` 目录
4. 选择您想要使用的字体

## 重新生成字体（可选）

如果需要修改字符集或字体参数：

```bash
cd main/ui

# 生成单个字体
lv_font_conv --size 16 --bpp 4 \
  --font GenJyuuGothic-Monospace-Light-2.ttf \
  --symbols "$(cat common_chinese_chars.txt)" \
  --format bin -o GenJyuuGothic-Monospace-Light-16.bin \
  --force-fast-kern-format

# 生成多个尺寸
for size in 14 16 18 20; do
  lv_font_conv --size $size --bpp 4 \
    --font GenJyuuGothic-Monospace-Light-2.ttf \
    --symbols "$(cat common_chinese_chars.txt)" \
    --format bin -o GenJyuuGothic-Monospace-Light-$size.bin \
    --force-fast-kern-format
done
```

## 内置字体 vs SD 卡字体

- **内置字体** (`lv_font_builtin_chinese_16`): 16px，编译到 Flash 中，始终可用
- **SD 卡字体**: 多种尺寸可选，需要 SD 卡，灵活性更高

两种字体使用相同的 491 个常用汉字，可以无缝切换。
