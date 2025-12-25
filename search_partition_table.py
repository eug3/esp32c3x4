#!/usr/bin/env python3
# 搜索 flash_dump.bin 中的分区表位置

import struct

def find_partition_table(data):
    """搜索分区表的位置（查找 0xAA50 魔数）"""
    results = []
    
    # 搜索所有可能的分区表起始位置
    for offset in range(0, len(data), 0x1000):  # 按 4KB 对齐搜索
        if offset + 32 > len(data):
            break
            
        # 检查是否是有效的分区表条目
        magic = struct.unpack('<H', data[offset:offset+2])[0]
        if magic == 0xAA50:
            # 尝试解析这个位置的分区表
            partitions = []
            pos = offset
            
            while pos + 32 <= len(data):
                entry = data[pos:pos+32]
                entry_magic = struct.unpack('<H', entry[0:2])[0]
                
                if entry_magic == 0xAA50:
                    ptype = entry[2]
                    subtype = entry[3]
                    poffset = struct.unpack('<I', entry[4:8])[0]
                    psize = struct.unpack('<I', entry[8:12])[0]
                    label = entry[12:28].decode('ascii', errors='ignore').rstrip('\x00')
                    
                    # 验证这个分区条目是否合理
                    if poffset > 0 and poffset < len(data) and psize > 0 and psize < len(data):
                        partitions.append({
                            'label': label,
                            'type': ptype,
                            'subtype': subtype,
                            'offset': poffset,
                            'size': psize
                        })
                        pos += 32
                    else:
                        break
                elif entry_magic == 0xFFFF:
                    break
                else:
                    break
            
            if partitions:
                results.append({
                    'location': offset,
                    'partitions': partitions
                })
    
    return results

# 读取 flash dump
with open('flash_dump.bin', 'rb') as f:
    flash_data = f.read()

print("搜索 Flash 中的分区表...")
print("=" * 80)

tables = find_partition_table(flash_data)

if tables:
    print(f"找到 {len(tables)} 个可能的分区表位置:\n")
    
    for idx, table in enumerate(tables):
        print(f"\n位置 {idx+1}: 0x{table['location']:08X}")
        print("-" * 80)
        print(f"{'标签':<16} {'类型':<6} {'子类型':<8} {'偏移':<12} {'大小'}")
        print("-" * 80)
        
        for p in table['partitions']:
            type_str = {0x00: 'app', 0x01: 'data'}.get(p['type'], f'0x{p["type"]:02X}')
            
            if p['type'] == 0x00:  # app
                subtype_str = {
                    0x00: 'factory',
                    0x10: 'ota_0',
                    0x11: 'ota_1',
                }.get(p['subtype'], f'0x{p["subtype"]:02X}')
            elif p['type'] == 0x01:  # data
                subtype_str = {
                    0x00: 'ota',
                    0x01: 'phy',
                    0x02: 'nvs',
                    0x82: 'spiffs',
                }.get(p['subtype'], f'0x{p["subtype"]:02X}')
            else:
                subtype_str = f'0x{p["subtype"]:02X}'
            
            print(f"{p['label']:<16} {type_str:<6} {subtype_str:<8} 0x{p['offset']:08X}  {p['size']//1024:>6} KB")
        
        print()
else:
    print("未找到有效的分区表！")
    print()
    print("让我检查 bootloader 区域...")
    
    # 检查 bootloader 魔数
    bootloader_magic = flash_data[0:4]
    print(f"0x0000: {bootloader_magic.hex()}")
    
    # 检查常见位置
    for offset in [0x8000, 0x9000, 0xD000, 0xE000]:
        data_at_offset = flash_data[offset:offset+16]
        print(f"0x{offset:04X}: {data_at_offset.hex()}")
