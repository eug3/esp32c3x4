# Flash 刷新指南

## 问题原因
分区表已更新（减少 LittleFS 从 6MB→4MB，增加 chapter_buf 2MB），但 Flash 中的旧字体数据仍在原位置。需要**全量重新刷新分区**。

## 刷机步骤

### 1. 确保设备处于 DFU 模式
```bash
# 停止任何正在运行的应用
# 按住 BOOT 按钮，点击 RESET，然后释放 BOOT

# 验证设备连接
adb devices -l  # 如果是 Android 设备
# 或
esptool.py -p /dev/ttyUSB0 chip_id  # 对于串口连接
```

### 2. 擦除整个 Flash（可选但推荐）
```bash
cd /Users/beijihu/Github/esp32c3x4/build

# 使用 esptool 擦除全部
esptool.py -p /dev/ttyUSB0 erase_flash
```

### 3. 刷写新的 bootloader + 分区表 + 应用
```bash
cd /Users/beijihu/Github/esp32c3x4/build

# 一键刷新（包含 bootloader、分区表、应用）
esptool.py -p /dev/ttyUSB0 -b 460800 --before default-reset --after hard-reset write_flash \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 monster-c3x4.bin
```

或者使用 idf.py（推荐）：
```bash
cd /Users/beijihu/Github/esp32c3x4
idf.py -p /dev/ttyUSB0 erase-flash flash
```

### 4. 刷写数据分区
```bash
cd /Users/beijihu/Github/esp32c3x4

# 刷写 GBK 编码表
esptool.py -p /dev/ttyUSB0 write_flash 0xf10000 data/gbk_table.bin

# 刷写字体数据（到新位置 0xa10000）
esptool.py -p /dev/ttyUSB0 write_flash 0xa10000 data/msyh-14.25pt.19×25.bin
```

或使用 idf.py：
```bash
idf.py -p /dev/ttyUSB0 flash-data
```

### 5. 验证刷写成功
```bash
# 重启设备
esptool.py -p /dev/ttyUSB0 read_mac

# 检查日志（应该看到）
# I (XXX) FONT_PART: Font partition mmap'd at 0xXXXXXXXX
# I (XXX) CHAPTER_BUF: Partition mmap'd at 0xXXXXXXXX

# 不应该再出现：
# E (36850) mmap: esp_mmu_map(477): no such vaddr range
# E (36850) FONT_PART: Failed to mmap font partition
```

## 如果使用 idf.py（推荐方式）

只需一条命令全量刷新：
```bash
cd /Users/beijihu/Github/esp32c3x4
idf.py -p /dev/ttyUSB0 erase-flash flash flash-data
```

这会按顺序执行：
1. 擦除整个 Flash
2. 烧写 bootloader + 分区表 + 应用
3. 烧写所有数据分区（字体、GBK 等）

## 设备端口查询

### macOS
```bash
ls /dev/tty.*
# 找到类似 /dev/tty.SLAB_USBtoUART 的设备
```

### Linux
```bash
ls /dev/ttyUSB*
# 或
ls /dev/ttyACM*
```

### 在 VS Code 中使用 idf.py
按 Cmd+Shift+P，搜索 "ESP-IDF: Flash" 或 "ESP-IDF: Erase Flash"

## 新的分区布局
刷新后，Flash 中的分区将按以下顺序排列：

| 分区 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| nvs | 0x9000 | 24KB | NVS 存储 |
| phy_init | 0xf000 | 4KB | PHY 初始化数据 |
| factory | 0x10000 | 4MB | 应用程序 |
| littlefs | 0x410000 | 4MB | 文件系统（**新：缩小**） |
| chapter_buf | 0x810000 | 2MB | **章节缓冲区（新增）** |
| font_data | 0xa10000 | 5MB | 字体数据（**新位置**） |
| gbk_table | 0xf10000 | 64KB | GBK 编码表 |

## 常见问题

**Q: 是否必须擦除整个 Flash？**
A: 不必须，但强烈推荐。这样可以避免旧数据干扰。

**Q: 刷新后为什么还是报错？**
A: 检查：
- 是否真的刷写到了新位置（0xa10000）？
- 字体文件是否完整（检查文件大小）？
- 设备是否正确重启（看 bootloader 日志）？

**Q: 可以只刷新字体分区吗？**
A: 可以，但由于分区位置改变，必须：
1. 先刷新新的分区表（0x8000）
2. 再刷新字体到新位置（0xa10000）

**Q: 刷新需要多长时间？**
A: 
- erase_flash：~10 秒
- 烧写应用：~5 秒
- 烧写数据：~2 秒
- 总计：~20 秒

## 刷新命令速查

### 快速刷新（推荐）
```bash
cd /Users/beijihu/Github/esp32c3x4
idf.py -p /dev/ttyUSB0 erase-flash flash flash-data
```

### 仅刷新应用（不改分区）
```bash
idf.py -p /dev/ttyUSB0 flash
```

### 仅刷新数据分区
```bash
idf.py -p /dev/ttyUSB0 flash-data
```

### 检查分区表
```bash
idf.py partition-table
```
