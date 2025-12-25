#!/usr/bin/env python3
# 分析 flash_dump.bin 并提取分区表信息

import struct
import sys

def parse_partition_table(data):
    """解析 ESP32 分区表"""
    partitions = []
    offset = 0
    
    while offset < len(data):
        # ESP32 分区表条目格式：
        # 2 bytes: magic (0x50AA - little endian)
        # 1 byte: type
        # 1 byte: subtype  
        # 4 bytes: offset
        # 4 bytes: size
        # 16 bytes: label
        # 4 bytes: flags
        
        if offset + 32 > len(data):
            break
            
        entry = data[offset:offset+32]
        magic = struct.unpack('<H', entry[0:2])[0]
        
        if magic == 0x50AA:  # 有效分区条目 (0xAA50 in big endian)
            ptype = entry[2]
            subtype = entry[3]
            poffset = struct.unpack('<I', entry[4:8])[0]
            psize = struct.unpack('<I', entry[8:12])[0]
            label = entry[12:28].decode('ascii', errors='ignore').rstrip('\x00')
            flags = struct.unpack('<I', entry[28:32])[0]
            
            type_name = {
                0x00: 'app',
                0x01: 'data',
            }.get(ptype, f'type_{ptype:02x}')
            
            subtype_name = ''
            if ptype == 0x00:  # app
                subtype_name = {
                    0x00: 'factory',
                    0x10: 'ota_0',
                    0x11: 'ota_1',
                    0x12: 'ota_2',
                    0x20: 'test',
                }.get(subtype, f'subtype_{subtype:02x}')
            elif ptype == 0x01:  # data
                subtype_name = {
                    0x00: 'ota',
                    0x01: 'phy',
                    0x02: 'nvs',
                    0x03: 'coredump',
                    0x04: 'nvs_keys',
                    0x05: 'efuse',
                    0x82: 'spiffs',
                    0x83: 'fat',
                }.get(subtype, f'subtype_{subtype:02x}')
            
            partitions.append({
                'label': label,
                'type': type_name,
                'subtype': subtype_name,
                'offset': poffset,
                'size': psize,
                'flags': flags
            })
        elif magic == 0xFFFF:  # 空白/结束
            break
            
        offset += 32
    
    return partitions

# 读取 flash dump
with open('flash_dump.bin', 'rb') as f:
    flash_data = f.read()

print("=" * 80)
print("ESP32-C3 Flash Dump 分析")
print("=" * 80)
print()

# 提取并分析分区表（通常在 0x8000）
partition_table_offset = 0x8000
partition_table_data = flash_data[partition_table_offset:partition_table_offset+0xC00]

print("分区表信息 (位于 0x8000):")
print("-" * 80)
partitions = parse_partition_table(partition_table_data)

if partitions:
    print(f"{'标签':<16} {'类型':<10} {'子类型':<12} {'偏移':<12} {'大小':<12} {'标志'}")
    print("-" * 80)
    for p in partitions:
        print(f"{p['label']:<16} {p['type']:<10} {p['subtype']:<12} 0x{p['offset']:08X}  0x{p['size']:08X}  0x{p['flags']:08X}")
else:
    print("未找到有效分区表！")

print()
print("=" * 80)
print("建议：")
print("=" * 80)

# 查找是否有 OTA 分区
has_ota = any(p['subtype'] in ['ota_0', 'ota_1'] for p in partitions)
factory_app = next((p for p in partitions if p['subtype'] == 'factory'), None)
ota_0 = next((p for p in partitions if p['subtype'] == 'ota_0'), None)

if factory_app:
    print(f"✓ 发现 factory 应用分区在 0x{factory_app['offset']:08X}")
    print(f"  大小: 0x{factory_app['size']:08X} ({factory_app['size']//1024}KB)")
    print()
    print("方案 1: 替换 factory 应用")
    print(f"  烧录命令: esptool -p COM5 write_flash 0x{factory_app['offset']:08X} your_app.bin")
    print()

if ota_0:
    print(f"✓ 发现 OTA_0 分区在 0x{ota_0['offset']:08X}")
    print(f"  大小: 0x{ota_0['size']:08X} ({ota_0['size']//1024}KB)")
    print()
    print("方案 2: 使用 OTA 机制")
    print(f"  烧录命令: esptool -p COM5 write_flash 0x{ota_0['offset']:08X} your_app.bin")
    print("  然后设置 OTA 数据分区选择 ota_0")

print()
print("⚠️  注意：")
print("  - bootloader 在 0x0，不要覆盖")
print("  - 分区表在 0x8000，不要覆盖")
print("  - NVS/其他数据分区保持不动")
