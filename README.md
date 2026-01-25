# 阅星瞳 X4 ESP32-C3 电子书阅读器

| 硬件平台 | 参数                      |
|----------|--------------------------|
| 主控芯片 | ESP32-C3 @ 160MHz        |
| Flash    | 16MB                     |
| 显示屏   | 4.26" 电子墨水屏 (800x480) |
| RAM      | 400KB                   |
| 字体     | 微软雅黑 14.25pt (19x25) |

## 简介

阅星瞳 X4 是一款基于 ESP32-C3 的电子墨水屏阅读器，提供完整的固件支持。支持 TXT 和 EPUB 格式电子书阅读、图片查看、蓝牙文件传输等功能，配备智能预缓存和阅读历史管理，带来流畅的阅读体验。

## 核心功能

### 阅读功能
- **TXT 阅读器** - 支持 GB18030/UTF-8 编码自动检测和转换
- **EPUB 阅读器** - 完整的 EPUB 2.0/3.0 支持，带章节导航
- **智能预缓存** - 滑动窗口预缓存机制，翻页速度提升 **16倍** (800ms → 50ms)
- **阅读历史** - 自动记录阅读位置，支持多本书籍记录（最多 10 本）
- **字体管理** - 支持 Flash 分区字体，可扩展自定义字体

### 系统功能
- **文件浏览器** - SD 卡文件管理，支持图片、电子书分类浏览
- **图片查看器** - 支持 PNG/JPEG/BMP 格式
- **壁纸管理** - 自定义设备壁纸
- **蓝牙传输** - BLE 协议，支持无线文件传输和书籍下载
- **低功耗** - 睡眠模式，长按电源键唤醒
- **电池管理** - 实时电量显示

## 硬件要求

- 阅星瞳 X4 开发板（ESP32-C3 主控）
- 16MB Flash
- 4.26" 电子墨水屏 (800x480)
- MicroSD 卡（支持书籍和图片存储）
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
```

## 项目结构

```
esp32c3x4/
├── main/                           # 主应用程序代码
│   ├── ui/                         # UI 相关代码
│   │   ├── epub/                   # EPUB 阅读器模块
│   │   │   ├── epub_parser.c/h     # EPUB 解析器
│   │   │   ├── epub_precache.c/h   # 章节预缓存管理
│   │   │   ├── epub_xml.cpp        # XML 解析
│   │   │   ├── epub_zip.c/h        # ZIP 解压缩
│   │   │   ├── epub_html.c/h       # HTML 渲染
│   │   │   └── chapter_buffer.c/h  # 章节缓冲区
│   │   ├── txt/                    # TXT 阅读器模块
│   │   │   ├── txt_reader.c/h      # TXT 文件读取
│   │   │   └── gb18030_conv.c/h    # GBK/GB18030 编码转换
│   │   ├── screens/                # 屏幕界面模块
│   │   │   ├── home_screen.c/h     # 主屏幕
│   │   │   ├── reader_screen.c/h   # 阅读屏幕
│   │   │   ├── file_browser_screen.c/h # 文件浏览器
│   │   │   ├── image_viewer_screen.c/h # 图片查看器
│   │   │   ├── ble_reader_screen.c/h   # BLE 阅读器
│   │   │   ├── settings_screen.c/h     # 设置屏幕
│   │   │   ├── font_select_screen.c/h  # 字体选择
│   │   │   └── wallpaper_screen.c/h    # 壁纸设置
│   │   ├── ble/                    # 蓝牙模块
│   │   │   ├── ble_manager.c/h         # BLE 管理
│   │   │   ├── ble_book_protocol.c/h   # BLE 书籍传输协议
│   │   │   ├── ble_cache_manager.c/h   # BLE 缓存管理
│   │   │   └── ble_write_queue.c/h     # BLE 写队列
│   │   ├── fonts/                 # 字体管理模块
│   │   │   ├── font_manager.c/h   # 字体管理器
│   │   │   ├── font_cache.c/h     # 字体缓存
│   │   │   ├── font_partition.c/h # Flash 字体分区读写
│   │   │   └── xt_eink_font.c/h   # 电子墨水屏字体渲染
│   │   ├── wallpaper/             # 壁纸管理
│   │   │   ├── wallpaper_manager.c/h
│   │   │   └── wallpaper_screen.c/h
│   │   ├── reading_history.c/h    # 阅读历史管理
│   │   ├── display_engine.c/h     # 显示引擎
│   │   ├── screen_manager.c/h     # 屏幕管理器
│   │   └── input_handler.c/h      # 输入处理
│   ├── lib/                       # 第三方库
│   │   ├── DEV_Config.c/h         # 硬件配置
│   │   ├── EPD_4in26.c/h          # 电子墨水屏驱动
│   │   ├── GUI_Paint.c/h          # 绘图库
│   │   └── ...
│   ├── Fonts/                     # 内置字体文件
│   ├── power_manager.c/h          # 电源管理
│   └── main.c                     # 应用入口
├── c3x4_main_control/              # ESP-IDF 平台适配层
│   ├── managed_components/         # 托管组件
│   └── tools/                     # 工具脚本
├── examples/                      # 示例项目
│   ├── xteink-x4-sample/          # 原厂示例
│   ├── monster_c3x4/               # C3X4 主控示例
│   ├── diy-esp32-epub-reader/    # EPUB 阅读器示例
│   └── ...
├── docs/                          # 文档
│   ├── EPUB_PRECACHE_*.md         # EPUB 预缓存文档
│   ├── READING_HISTORY_*.md      # 阅读历史文档
│   ├── BLE_*.md                  # BLE 相关文档
│   └── ...
├── data/                          # 数据文件（烧录到 Flash）
│   ├── msyh-14.25pt.19×25.bin    # 微软雅黑字体文件
│   └── gbk_table.bin              # GBK 编码转换表
├── tools/                         # 工具脚本
│   ├── generate_gbk_table.py      # GBK 编码表生成工具
│   ├── flash.py                  # Flash 烧录脚本
│   └── generate_version.py        # 版本生成脚本
├── partitions.csv                 # Flash 分区表配置
├── CMakeLists.txt                 # 项目 CMake 配置
├── sdkconfig                      # SDK 配置文件
├── CHANGELOG.md                   # 功能更新日志
├── MEMORY_OPTIMIZATION.md          # 内存优化方案
├── FLASH_GUIDE.md                 # Flash 刷新指南
└── README.md                      # 本文档
```

## Flash 分区方案 (16MB)

| 分区        | 用途         | 大小   | 偏移地址    |
|-------------|------------|--------|------------|
| nvs         | NVS 存储    | 24KB   | 0x9000     |
| phy_init    | PHY 校准    | 4KB    | 0xf000     |
| factory     | 应用程序    | 4MB    | 0x10000    |
| littlefs    | 用户数据    | 4MB    | 0x410000   |
| chapter_buf | 章节缓冲    | 2MB    | 0x810000   |
| font_data   | 字体文件    | 5MB    | 0xa10000   |
| gbk_table   | GBK 编码表  | 64KB   | 0xf10000   |

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

### 烧录数据分区

```bash
# 烧录字体文件和 GBK 编码表
idf.py -p /dev/ttyUSB0 flash-data
```

### 一键完整烧录

```bash
# 擦除 Flash + 烧录固件 + 烧录数据
idf.py -p /dev/ttyUSB0 erase-flash flash flash-data
```

### 烧录完整 16MB Flash Bin

使用预合并的完整固件（包含应用 + 字体 + GBK 表）：

```bash
# 烧录完整 16MB bin 文件
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 460800 \
  write_flash 0x0 build/monster-c3x4-16m-full.bin
```

### 备份 Flash 内容

备份设备上的完整 Flash 内容（用于固件恢复或分析）：

```bash
# 备份完整 16MB Flash 到文件
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 460800 \
  read_flash 0x0 0x1000000 flash_backup.bin

# 只备份应用程序分区
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 460800 \
  read_flash 0x10000 0x400000 app_backup.bin
```

### 监控串口输出

```bash
idf.py -p /dev/ttyUSB0 monitor
```

按 `Ctrl+]` 退出监控。

## 核心功能说明

### EPUB 智能预缓存

EPUB 阅读器配备滑动窗口预缓存机制，显著提升翻页速度：

- **配置**: 当前章节前 2 章 + 后 5 章
- **性能**: 翻页从 800ms 降至 50ms（16倍提升）
- **占用**: 典型场景 200-500 KB Flash 空间

**使用方法**:

```c
#include "epub_precache.h"

void app_main(void) {
    // ... 其他初始化 ...
    epub_precache_init();  // 就这一行！
    // ... 其他代码 ...
}
```

详细信息请查看: [EPUB_PRECACHE_QUICKREF.md](docs/EPUB_PRECACHE_QUICKREF.md)

### 阅读历史管理

自动记录每本书的阅读位置和最近阅读的书籍列表：

- **自动记录**: 章节跳转时自动保存位置
- **最近阅读**: 维护最多 10 本最近阅读的书
- **快速恢复**: 打开书籍自动恢复上次位置
- **持久化**: NVS Flash 存储，重启不丢失

**使用方法**:

```c
#include "reading_history.h"

void app_main(void) {
    nvs_flash_init();
    reading_history_init();  // 就这一行！
}
```

详细信息请查看: [READING_HISTORY_QUICKREF.md](docs/READING_HISTORY_QUICKREF.md)

### BLE 文件传输

支持通过蓝牙无线传输文件到设备：

- **协议**: 自定义 X4IM 协议
- **功能**: 文件列表、上传、下载、删除、重命名
- **性能**: 优化的缓存机制和队列管理

详细信息请查看: [BLE_SD_FILE_MANAGER_ESP32.md](docs/BLE_SD_FILE_MANAGER_ESP32.md)

## 数据文件说明

| 文件                              | 大小     | 用途说明                       |
|-----------------------------------|----------|------------------------------|
| `data/msyh-14.25pt.19×25.bin`     | ~4.9MB   | 微软雅黑字体，19x25 像素       |
| `data/gbk_table.bin`              | 64KB     | GBK/GB18030 到 Unicode 编码转换表 |

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

### Flash 分区错误

如果遇到 `no such vaddr range` 错误，需要完整刷新：

```bash
idf.py -p /dev/ttyUSB0 erase-flash flash flash-data
```

详细信息请查看: [FLASH_GUIDE.md](FLASH_GUIDE.md)

## 性能优化

### 内存优化

本项目已实施多项内存优化方案：

- **编码转换表移到 Flash**: 节省 174KB RAM
- **字体表移到 Flash**: 节省 30KB RAM
- **BLE 缓冲区优化**: 节省 10-15KB RAM
- **启动动画移到 Flash**: 节省 20KB RAM

详细信息请查看: [MEMORY_OPTIMIZATION.md](MEMORY_OPTIMIZATION.md)

### EPUB 解析优化

- 流式解析，降低内存占用
- 章节级缓存，减少重复解析
- 异步分页设计，提升响应速度

详细信息请查看: [EPUB_PARSER_IMPROVEMENTS.md](docs/EPUB_PARSER_IMPROVEMENTS.md)

## 版本历史

详细的功能更新记录请查看 [CHANGELOG.md](CHANGELOG.md)

### 最新更新

- **2026-01-09**: 阅读历史管理功能
- **2026-01-09**: EPUB 章节预缓存功能

## 技术支持与反馈

- 如有问题，请访问 [ESP32 论坛](https://esp32.com/) 进行咨询
- 提交 Bug 或功能建议：请在项目 GitHub 仓库创建 Issue

## 文档索引

### 核心功能文档
- [EPUB 预缓存快速参考](docs/EPUB_PRECACHE_QUICKREF.md)
- [阅读历史快速参考](docs/READING_HISTORY_QUICKREF.md)
- [EPUB 解析流程](docs/EPUB_PARSING_PROCESS.md)

### 设计文档
- [EPUB 预缓存设计](docs/EPUB_PRECACHE_DESIGN.md)
- [阅读历史指南](docs/READING_HISTORY_GUIDE.md)
- [BLE 流式修复](docs/BLE_STREAMING_FIX.md)

### 实现文档
- [EPUB 预缓存实现](docs/EPUB_PRECACHE_IMPLEMENTATION.md)
- [TXT 阅读器实现](docs/TXT_READER_IMPLEMENTATION.md)
- [阅读历史实现](docs/READING_HISTORY_SUMMARY.md)

### 故障排查
- [Flash 刷新指南](FLASH_GUIDE.md)
- [内存优化方案](MEMORY_OPTIMIZATION.md)
- [BLE 断连修复](docs/BLE_DISCONNECT_CRASH_FIX.md)

## 开源协议

本项目基于 Apache 2.0 协议开源。

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 开发框架
- [LittleFS](https://github.com/littlefs-project/littlefs) - 文件系统
- 微软雅黑字体
- EPUB 阅读器开源项目
