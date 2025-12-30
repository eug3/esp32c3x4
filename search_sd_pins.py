#!/usr/bin/env python3
# 在 flash dump 中搜索 SD 卡和 EPD 引脚配置
# 通过搜索可能的引脚号序列

import struct

with open('d:\\GitHub\\esp32c3x4\\flash_dump.bin', 'rb') as f:
    flash_data = f.read()

print("=" * 80)
print("ESP32-C3 Flash Dump 引脚配置搜索")
print("=" * 80)

# app0 从 0x10000 开始
app_start = 0x10000
app_size = 0x640000
app_data = flash_data[app_start:app_start + app_size]

print(f"\n分析 app0 区域 (0x{app_start:08X} - 0x{app_start + app_size:08X})")
print("=" * 80)

# 常见的 ESP32-C3 SPI 引脚配置
# SD 卡 (SPI): 通常是 MISO=6, MOSI=7, SCK=8, CS=9
# EPD (SPI): 可能是 SCK=10, MOSI=8, CS=4, DC=5, RST=6, BUSY=7

# 搜索连续的引脚号序列 (小端序 DWORD)
def search_gpio_sequence(data, gpio_list, description):
    """搜索包含指定 GPIO 号码的序列"""
    print(f"\n搜索 {description}:")
    print(f"  目标序列: {[hex(g) for g in gpio_list]}")

    # 将 GPIO 列表转换为字节序列 (小端序 U32)
    target_bytes = bytes()
    for gpio in gpio_list:
        target_bytes += struct.pack('<I', gpio)

    # 在数据中搜索
    pos = data.find(target_bytes)
    if pos != -1:
        abs_pos = app_start + pos
        print(f"  [+] Found at 0x{abs_pos:08X}")

        # 显示附近的数据
        print(f"  附近数据:")
        for i in range(-16, 32, 4):
            offset = pos + i
            if 0 <= offset < len(data) - 4:
                val = struct.unpack('<I', data[offset:offset+4])[0]
                marker = " <--" if i == 0 else ""
                print(f"    [0x{app_start + offset:08X}] 0x{val:08X} ({val}){marker}")
        return True
    else:
        print(f"  [-] Not found")
        return False

# 已知的 SD 卡引脚配置
sd_pins_user = [6, 7, 8, 9]  # MISO, MOSI, CLK, CS

# 从 IDA 分析的 EPD 引脚配置
epd_pins_ida = [4, 5, 6, 7, 8, 9]  # CS, DC, RST, BUSY, MOSI, SCK

# 用户要求的 EPD 引脚配置
epd_pins_user = [9, 10, 12, 17]  # SCK, MOSI, DC, CS

# 搜索各种可能的配置
search_gpio_sequence(app_data, sd_pins_user, "SD 卡引脚 (MISO=6, MOSI=7, CLK=8, CS=9)")
search_gpio_sequence(app_data, epd_pins_ida, "IDA 分析的 EPD 引脚 (4,5,6,7,8,9)")
search_gpio_sequence(app_data, epd_pins_user, "用户要求的 EPD 引脚 (9,10,12,17)")

# 搜索单个引脚号的出现位置
print("\n" + "=" * 80)
print("搜索单个 GPIO 号码的出现位置 (小端序 DWORD)")
print("=" * 80)

common_gpios = [4, 5, 6, 7, 8, 9, 10, 12, 17]
gpio_positions = {}

for gpio in common_gpios:
    gpio_bytes = struct.pack('<I', gpio)
    pos = 0
    count = 0
    first_pos = None

    # 限制搜索范围
    while pos < min(len(app_data), 0x500000):
        pos = app_data.find(gpio_bytes, pos)
        if pos == -1:
            break
        if first_pos is None:
            first_pos = pos
        count += 1
        pos += 4

        if count >= 10:  # 只统计前10个
            break

    if count > 0:
        abs_pos = app_start + first_pos
        gpio_positions[gpio] = (count, abs_pos)

        # 显示这个 GPIO 的第一个出现位置附近的值
        print(f"\nGPIO {gpio}: 找到 {count}+ 次，首次在 0x{abs_pos:08X}")
        for i in range(-16, 32, 4):
            offset = first_pos + i
            if 0 <= offset < len(app_data) - 4:
                val = struct.unpack('<I', app_data[offset:offset+4])[0]
                marker = " <--" if i == 0 else ""
                if val in common_gpios:
                    print(f"  [0x{app_start + offset:08X}] 0x{val:08X} ({val}){marker}")

# 尝试找到包含多个 GPIO 的配置数组
print("\n" + "=" * 80)
print("分析可能的配置数组 (包含 3+ 个 GPIO 的连续位置)")
print("=" * 80)

# 扫描数据，查找连续包含多个 GPIO 的位置
scan_end = min(len(app_data), 0x100000)  # 只扫描前 1MB
for offset in range(0, scan_end - 24, 4):
    # 检查连续 6 个 DWORD
    vals = []
    for i in range(6):
        val = struct.unpack('<I', app_data[offset + i*4:offset + i*4 + 4])[0]
        vals.append(val)

    # 检查是否都是有效的 GPIO 号码
    if all(v in common_gpios for v in vals):
        abs_offset = app_start + offset
        print(f"\n在 0x{abs_offset:08X} 发现 GPIO 配置数组:")
        for i, v in enumerate(vals):
            print(f"  [0x{abs_offset + i*4:08X}] GPIO {v}")
        # 只显示前 5 个这样的数组
        break

print("\n" + "=" * 80)
print("搜索完成")
print("=" * 80)
