#!/usr/bin/env python3
"""
生成 OTA Data 分区切换脚本
用于切换 ESP32-C3 启动到指定的 OTA 分区
"""

import struct
import binascii

def create_otadata(target_ota):
    """
    创建 OTA Data 分区二进制文件
    
    OTA Data 格式：
    - 总大小: 8KB (0x2000)
    - 包含 2 个条目，每个 4KB
    - 每个条目格式:
      - uint32 seq: 序列号（递增）
      - uint32 ota_seq: OTA 分区序号（0=ota_0, 1=ota_1）
      - uint32 crc32: CRC32 校验
      - 剩余填充 0xFF
    """
    
    # 创建 8KB 空间，填充 0xFF
    otadata = bytearray([0xFF] * 0x2000)
    
    # OTA 分区映射
    ota_map = {
        'ota_0': 0,
        'ota_1': 1,
        'ota_2': 2
    }
    
    if target_ota not in ota_map:
        raise ValueError(f"无效的 OTA 分区: {target_ota}")
    
    ota_seq = ota_map[target_ota]
    
    # Entry 0: 设置为目标 OTA
    # 序列号从 1 开始
    seq = 1
    
    # 构建数据块（不含 CRC）
    entry_data = struct.pack('<I', seq) + struct.pack('<I', ota_seq)
    
    # 计算 CRC32（包含 seq 和 ota_seq）
    crc = binascii.crc32(entry_data) & 0xFFFFFFFF
    
    # 写入 Entry 0
    offset = 0
    otadata[offset:offset+4] = struct.pack('<I', seq)
    otadata[offset+4:offset+8] = struct.pack('<I', ota_seq)
    otadata[offset+8:offset+12] = struct.pack('<I', crc)
    
    # Entry 1: 保持无效（全 0xFF）
    # 默认已填充
    
    return otadata

# 生成所有可能的 OTA 切换文件
for ota in ['ota_0', 'ota_1']:
    filename = f'otadata_select_{ota}.bin'
    data = create_otadata(ota)
    
    with open(filename, 'wb') as f:
        f.write(data)
    
    print(f"✓ 创建 {filename}")
    print(f"  烧录到设备: esptool -p COM5 --chip esp32c3 write_flash 0xE000 {filename}")
    print()

print("=" * 60)
print("使用说明:")
print("=" * 60)
print()
print("1. 启动到 app0 (原厂固件):")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 otadata_select_ota_0.bin")
print()
print("2. 启动到 app1 (自定义应用):")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 otadata_select_ota_1.bin")
print()
print("3. 重启设备:")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 run")
