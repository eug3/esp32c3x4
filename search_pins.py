#!/usr/bin/env python3
# 在 flash dump 中搜索 GPIO 引脚相关字符串

with open('d:\\GitHub\\esp32c3x4\\flash_dump.bin', 'rb') as f:
    flash_data = f.read()

print("=" * 60)
print("搜索 GPIO 相关字符串")
print("=" * 60)

# 搜索可能的引脚相关字符串
keywords = [
    b'GPIO',
    b'gpio',
    b'EPD',
    b'epd',
    b'SCK',
    b'sck',
    b'MOSI',
    b'mosi',
    b'MISO',
    b'miso',
    b'CS',
    b'cs',
    b'DC',
    b'dc',
    b'RST',
    b'rst',
    b'BUSY',
    b'busy',
    b'SPI',
    b'spi',
]

# 提取主应用区域 (app0 从 0x10000 开始，大小 0x640000)
app_start = 0x10000
app_size = 0x640000
app_data = flash_data[app_start:app_start + app_size]

print(f"\n搜索 app0 区域 (0x{app_start:08X} - 0x{app_start + app_size:08X})")
print("-" * 60)

found = {}
for keyword in keywords:
    pos = 0
    while True:
        pos = app_data.find(keyword, pos)
        if pos == -1:
            break
        # 提取上下文
        context_start = max(0, pos - 30)
        context_end = min(len(app_data), pos + 50)
        context = app_data[context_start:context_end]
        try:
            context_str = context.decode('ascii', errors='ignore').strip()
            abs_pos = app_start + pos
            if keyword not in found:
                found[keyword.decode('ascii', errors='ignore')] = []
            found[keyword.decode('ascii', errors='ignore')].append((abs_pos, context_str))
        except:
            pass
        pos += 1

if found:
    for kw, locations in sorted(found.items()):
        print(f"\n{kw}:")
        for pos, ctx in locations[:5]:  # 只显示前5个
            print(f"  0x{pos:08X}: ...{ctx}...")
else:
    print("未找到 GPIO 相关字符串")

print("\n" + "=" * 60)
print("搜索数字模式 (可能代表引脚号)")
print("=" * 60)

# 搜索类似 "GPIO_NUM_4" 或 "gpio_num":4" 的模式
import re

# 尝试将部分数据解码为 ASCII 并搜索
chunk_size = 1024 * 100  # 每次 100KB
for offset in range(0, min(len(app_data), 1024 * 1024), chunk_size):
    chunk = app_data[offset:offset + chunk_size]
    try:
        text = chunk.decode('ascii', errors='ignore')
        # 搜索 GPIO_NUM_X 模式
        matches = re.findall(r'GPIO[_\s]?NUM[_\s]?(\d+)', text)
        if matches:
            abs_offset = app_start + offset
            print(f"\n在 0x{abs_offset:08X} 附近找到:")
            for m in set(matches):
                print(f"  GPIO_NUM_{m}")
        # 搜索 gpio_num:(\d) 模式
        matches = re.findall(r'gpio[_\s]?num["\':]?\s*(\d+)', text)
        if matches:
            abs_offset = app_start + offset
            print(f"\n在 0x{abs_offset:08X} 附近找到:")
            for m in set(matches):
                print(f"  gpio_num: {m}")
    except:
        pass

print("\n搜索完成")
