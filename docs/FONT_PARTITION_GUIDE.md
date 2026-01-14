# 字体分区使用指南

## 概述

字体分区是一个独立的 Flash 分区，用于存储字体文件。使用字体分区后，即使没有 SD 卡，设备也能正常显示文字。

## 分区配置

在 `partitions.csv` 中定义：

```csv
font_data,  data, spiffs,  0x810000, 0x600000,  # 6MB 字体分区
```

- **起始地址**：0x810000 (8MB + 4MB)
- **大小**：6MB
- **类型**：DATA/SPIFFS

## 烧录字体到分区

### 方法 1：使用提供的脚本（推荐）

```bash
# 烧录字体文件到分区
python tools/flash_font_partition.py /path/to/font.bin

# 示例
python tools/flash_font_partition.py /Volumes/SDCARD/fonts/msyh-14.25pt.19x25.bin
```

### 方法 2：手动使用 esptool

```bash
esptool.py --chip esp32c3 write_flash 0x810000 /path/to/font.bin
```

## 字体读取优先级

系统按以下优先级读取字体：

1. **LittleFS 缓存**（Flash 文件系统，最快）
   - 常用 3555 个字符的字形数据
   - 约 260KB

2. **字体分区**（Flash 分区，快速）
   - 完整 65536 个字符
   - 约 4.7MB
   - 无需 SD 卡

3. **SD 卡字体文件**（最慢）
   - 完整字体文件
   - 需要 SD 卡插入

## 工作流程

```
启动
  ↓
初始化 LittleFS 缓存 (level1_table 3555 字符)
  ↓
初始化字体分区 (可选，6MB Flash)
  ↓
尝试打开 SD 卡字体 (可选)
  ↓
渲染字符时:
  1. 查找 LittleFS 缓存 → 找到：返回
  2. 查找字体分区 → 找到：返回
  3. 读取 SD 卡文件 → 找到：返回
  4. 显示缺字方块
```

## 优势

### 使用字体分区的好处：

✅ **无 SD 卡运行**
- 即使没有 SD 卡，也能显示完整字符集
- 适合出厂固件或演示模式

✅ **访问速度快**
- Flash 读取速度远快于 SD 卡
- 减少 SD 卡频繁访问，延长寿命

✅ **降低功耗**
- 减少 SD 卡供电需求
- 适合低功耗应用

✅ **提高可靠性**
- 不依赖 SD 卡接触质量
- 避免 SD 卡意外拔出导致的问题

### 空间占用：

| 组件 | 大小 | 位置 |
|------|------|------|
| level1_table（字符索引） | 7KB | Flash Data (.rodata) |
| LittleFS 缓存（常用字） | 260KB | LittleFS 分区 |
| 字体分区（完整字体） | 4.7MB | font_data 分区 |
| **总计** | **~5MB** | **Flash** |

## 验证

烧录后可通过日志验证：

```
I (xxx) FONT_PART: Font partition found:
I (xxx) FONT_PART:   Label: font_data
I (xxx) FONT_PART:   Offset: 0x810000
I (xxx) FONT_PART:   Size: 6291456 bytes (6.00 MB)
I (xxx) XT_FONT_IMPL: Font partition initialized successfully
```

## 更新字体

重新烧录字体时，只需要更新字体分区，不影响应用程序：

```bash
# 只更新字体分区（快速）
python tools/flash_font_partition.py new_font.bin

# 完整更新（包含应用）
idf.py flash
```

## 故障排除

### 问题：字体分区未找到

**原因**：分区表未更新

**解决**：
```bash
# 擦除 Flash 并重新烧录
idf.py erase-flash
idf.py flash
```

### 问题：显示仍然依赖 SD 卡

**原因**：字体分区未烧录数据

**解决**：
```bash
python tools/flash_font_partition.py /path/to/font.bin
```

### 问题：部分字符显示为方块

**原因**：
1. 字体分区数据损坏
2. LittleFS 缓存未覆盖该字符
3. SD 卡字体文件缺失

**解决**：
1. 重新烧录字体分区
2. 检查 level1_table 是否包含该字符
3. 插入包含完整字体的 SD 卡

## 技术细节

### 字体格式

- **格式**：XTEink Font Binary (无头部)
- **字符数**：65536 (0x10000)
- **字符范围**：U+0000 ~ U+FFFF
- **字形大小**：固定（例如 19×25 = 75 字节）
- **总大小**：65536 × 字形大小

### 读取性能

| 数据源 | 延迟 | 带宽 |
|--------|------|------|
| LittleFS | ~1ms | ~1MB/s |
| Flash 分区 | ~0.5ms | ~2MB/s |
| SD 卡 | ~10ms | ~500KB/s |

## 参考

- 分区表：`partitions.csv`
- 字体分区代码：`main/ui/fonts/font_partition.c`
- 字体读取代码：`main/ui/fonts/xt_eink_font.c`
- 烧录工具：`tools/flash_font_partition.py`
