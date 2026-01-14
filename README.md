| 硬件平台 | 参数                      |
|----------|--------------------------|
| 主控芯片 | ESP32-C3 @ 160MHz        |
| Flash    | 16MB                     |
| 显示屏   | 电子墨水屏                |
| 字体     | 微软雅黑 19x25 像素       |

# 阅星瞳 X4 ESP32-C3 电子书阅读器

本项目是一个基于 ESP32-C3 的开源电子书阅读器应用，支持 TXT 文本阅读、GBK/GB18030 编码转换。

## 简介

阅星瞳 X4 是一款基于 ESP32-C3 的电子墨水屏阅读器，本项目为其提供固件支持。通过本固件，您可以在设备上阅读 TXT 格式的电子书，享受舒适的阅读体验。

## 功能特点

- **TXT 阅读** - 支持 TXT 文本文件阅读
- **编码自动检测** - 自动识别文件编码（UTF-8、GB18030、ASCII）
- **中文编码转换** - GBK/GB18030 到 UTF-8 转换
- **中文字体显示** - 集成微软雅黑字体，支持中文显示
- **阅读进度保存** - 自动保存和恢复阅读进度
- **低功耗设计** - 电子墨水屏特性，超低功耗阅读

## 硬件要求

- 阅星瞳 X4 开发板（ESP32-C3 主控）
- 16MB Flash
- 电子墨水屏显示屏
- USB 数据线（用于烧录和调试）

## 软件依赖

- ESP-IDF v5.x 或更高版本
- Python 3.8+
- Git

## 环境配置

### 1. 安装 ESP-IDF

```bash
# 克隆 ESP-IDF
git clone --depth 1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh all
source export.sh
```

### 2. 克隆本项目

```bash
git clone https://your-repo-url/esp32c3x4.git
cd esp32c3x4
git submodule update --init --recursive
```

## 项目结构

```
esp32c3x4/
├── main/                       # 主应用程序代码
│   ├── ui/                     # UI 相关代码
│   │   ├── fonts/              # 字体管理模块
│   │   │   ├── font_partition.c/h   # Flash 字体分区读写
│   │   │   └── xt_eink_font.c/h     # 电子墨水屏字体渲染
│   │   ├── screens/            # 屏幕界面模块
│   │   └── txt/                # TXT 阅读器
│   │       ├── txt_reader.c/h       # TXT 文件读取
│   │       └── gb18030_conv.c/h     # GBK/GB18030 编码转换
│   ├── CMakeLists.txt
│   └── main.c                  # 应用入口
├── data/                       # 数据文件（烧录到 Flash）
│   ├── msyh-14.25pt.19×25.bin  # 微软雅黑字体文件
│   └── gbk_table.bin           # GBK 编码转换表
├── tools/                      # 工具脚本
│   └── generate_gbk_table.py   # GBK 编码表生成工具
├── partitions.csv              # Flash 分区表配置
├── CMakeLists.txt              # 项目 CMake 配置
├── sdkconfig                   # SDK 配置文件
└── README.md                   # 本文档
```

## 编译与烧录

### 环境准备

```bash
# 设置 ESP-IDF 环境
source $IDF_PATH/export.sh

# 进入项目目录
cd esp32c3x4
```

### 编译项目

```bash
idf.py build
```

### 烧录固件

```bash
# 烧录到设备（请根据实际端口修改 /dev/ttyUSB0）
idf.py -p /dev/ttyUSB0 flash
```

### 监控串口输出

```bash
idf.py -p /dev/ttyUSB0 monitor
```

按 `Ctrl+]` 退出监控。

## 烧录说明

本项目使用 16MB Flash，分区方案如下：

| 分区     | 用途       | 大小  | 偏移地址    |
|----------|-----------|-------|------------|
| nvs      | NVS 存储   | 24KB  | 0x9000     |
| phy_init | PHY 校准   | 4KB   | 0xf000     |
| factory  | 应用程序   | 4MB   | 0x10000    |
| littlefs | 用户数据   | 6MB   | 0x410000   |
| font_data| 字体文件   | 5MB   | 0xa10000   |
| gbk_table| GBK 编码表 | 64KB  | 0xf10000   |

### 1. 编译并烧录固件

```bash
# 编译项目
idf.py build

# 烧录固件（自动烧录 factory、bootloader 和分区表）
idf.py -p /dev/ttyUSB0 flash
```

### 2. 烧录数据分区

固件烧录完成后，需要单独烧录字体文件和 GBK 编码表：

```bash
# 烧录字体文件到 font_data 分区
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset 0x8000 \
  write_partition --partition-name font_data \
  --input data/msyh-14.25pt.19×25.bin

# 烧录 GBK 编码表到 gbk_table 分区
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset 0x8000 \
  write_partition --partition-name gbk_table \
  --input data/gbk_table.bin
```

### 3. 一键烧录脚本

创建 `flash_all.sh` 脚本：

```bash
#!/bin/bash
PARTITION_OFFSET=0x8000
PORT=/dev/ttyUSB0

echo "========================================="
echo "  阅星瞳 X4 ESP32-C3 固件烧录脚本"
echo "========================================="

echo ""
echo "[1/4] 正在编译项目..."
idf.py build

echo ""
echo "[2/4] 正在烧录固件..."
idf.py -p $PORT flash

echo ""
echo "[3/4] 正在烧录 font_data 分区（字体文件）..."
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset $PARTITION_OFFSET \
  write_partition --partition-name font_data \
  --input data/msyh-14.25pt.19×25.bin

echo ""
echo "[4/4] 正在烧录 gbk_table 分区（GBK 编码表）..."
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset $PARTITION_OFFSET \
  write_partition --partition-name gbk_table \
  --input data/gbk_table.bin

echo ""
echo "========================================="
echo "  烧录完成！请重启设备。"
echo "========================================="
```

运行脚本：

```bash
chmod +x flash_all.sh
./flash_all.sh
```

## 数据文件说明

| 文件                              | 大小     | 用途说明                       |
|-----------------------------------|----------|------------------------------|
| `data/msyh-14.25pt.19×25.bin`     | ~4.9MB   | 微软雅黑字体，19x25 像素       |
| `data/gbk_table.bin`              | 64KB     | GBK/GB18030 到 Unicode 编码转换表 |
| `tools/generate_gbk_table.py`     | -        | GBK 编码表生成脚本             |

### 重新生成 GBK 编码表

如需更新 GBK 编码表，可运行：

```bash
cd tools
python generate_gbk_table.py
```

生成的 `gbk_table.bin` 会覆盖 `data/` 目录下的同名文件。

## 常见问题排查

### 程序上传失败

- **硬件连接不正确**：运行 `idf.py -p PORT monitor`，然后重启开发板查看日志输出。
- **下载波特率过高**：在 `menuconfig` 菜单中降低波特率，然后重试。
- **端口被占用**：确认设备管理器中显示的端口号，修改 `-p` 参数。

### 字体显示异常

- 确认 `font_data` 分区已正确烧录
- 检查字体文件大小是否正确（约 4.9MB）

### 中文显示为乱码

- 确认 `gbk_table` 分区已正确烧录
- 检查文件编码是否为 GBK/GB18030 或 UTF-8

## 技术支持与反馈

- 如有问题，请访问 [ESP32 论坛](https://esp32.com/) 进行咨询
- 提交 Bug 或功能建议：请在项目 GitHub 仓库创建 Issue

## 开源协议

本项目基于 Apache 2.0 协议开源。

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 开发框架
- [LittleFS](https://github.com/littlefs-project/littlefs) - 文件系统
- 微软雅黑字体
