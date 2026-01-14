| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | ESP32-S31 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | --------- | ----- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.

## 烧录说明

本项目使用 16MB Flash，分区方案如下：

| 分区   | 用途       | 大小  | 偏移地址    |
|--------|-----------|-------|------------|
| nvs    | NVS 存储   | 24KB  | 0x9000     |
| phy_init | PHY 校准  | 4KB   | 0xf000     |
| factory | 应用程序   | 4MB   | 0x10000    |
| littlefs | 用户数据   | 6MB   | 0x410000   |
| font_data | 字体文件   | 5MB   | 0xa10000   |
| gbk_table | GBK 编码表 | 64KB  | 0xf10000   |

### 1. 编译并烧录固件

```bash
# 编译项目
idf.py build

# 烧录固件（自动烧录 factory, bootloader, partition table）
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

### 3. 完整烧录脚本

```bash
#!/bin/bash
PARTITION_OFFSET=0x8000
PORT=/dev/ttyUSB0

echo "Building project..."
idf.py build

echo "Flashing firmware..."
idf.py -p $PORT flash

echo "Flashing font_data partition..."
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset $PARTITION_OFFSET \
  write_partition --partition-name font_data \
  --input data/msyh-14.25pt.19×25.bin

echo "Flashing gbk_table partition..."
python $IDF_PATH/components/esptool_py/parttool/parttool.py \
  --partition-table-offset $PARTITION_OFFSET \
  write_partition --partition-name gbk_table \
  --input data/gbk_table.bin

echo "Done!"
```

### 数据文件说明

- `data/msyh-14.25pt.19×25.bin` - 微软雅黑字体，19x25 像素
- `data/gbk_table.bin` - GBK/GB18030 到 Unicode 编码转换表
