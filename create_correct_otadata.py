#!/usr/bin/env python3
"""
为 XTEINK X4 创建正确的 OTA Data
基于原厂分区表分析：otadata 在 0xE000
"""

import struct
import binascii

def create_otadata_switch(target_app):
    """
    创建 OTA Data 用于切换启动分区
    
    target_app: 'app0' 或 'app1'
    """
    # 8KB otadata 分区
    otadata = bytearray([0xFF] * 0x2000)
    
    # OTA 序号映射
    ota_map = {
        'app0': 0,  # ota_0
        'app1': 1   # ota_1
    }
    
    if target_app not in ota_map:
        raise ValueError(f"Invalid app: {target_app}")
    
    ota_seq = ota_map[target_app]
    
    # Entry 0 (偏移 0x0)
    seq = 1
    entry_data = struct.pack('<I', seq) + struct.pack('<I', ota_seq)
    crc = binascii.crc32(entry_data) & 0xFFFFFFFF
    
    otadata[0:4] = struct.pack('<I', seq)
    otadata[4:8] = struct.pack('<I', ota_seq)
    otadata[8:12] = struct.pack('<I', crc)
    
    # Entry 1 (偏移 0x1000 = 4096) 保持全 0xFF
    
    return otadata

# 生成切换文件
print("=" * 70)
print("XTEINK X4 OTA Data 生成工具")
print("=" * 70)
print()
print("原厂分区表分析:")
print("  otadata 位置: 0xE000 (正确!)")
print("  app0 (ota_0): 0x10000")
print("  app1 (ota_1): 0x650000")
print()

# 生成 app0 切换文件
data_app0 = create_otadata_switch('app0')
with open('otadata_boot_app0.bin', 'wb') as f:
    f.write(data_app0)

print("✓ 创建 otadata_boot_app0.bin")
print(f"  前16字节: {data_app0[:16].hex()}")
print()

# 生成 app1 切换文件
data_app1 = create_otadata_switch('app1')
with open('otadata_boot_app1.bin', 'wb') as f:
    f.write(data_app1)

print("✓ 创建 otadata_boot_app1.bin")
print(f"  前16字节: {data_app1[:16].hex()}")
print()

print("=" * 70)
print("使用方法:")
print("=" * 70)
print()
print("1. 启动到 app0 (原厂固件):")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 otadata_boot_app0.bin")
print()
print("2. 启动到 app1 (自定义应用):")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 otadata_boot_app1.bin")
print()
print("3. 重启设备:")
print("   d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 run")
print()
print("⚠ 注意: 0xE000 是正确的 otadata 位置,不会破坏分区表!")
