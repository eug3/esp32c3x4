#!/usr/bin/env python3
# 从 flash_dump.bin 提取 app1 分区作为备份

import sys

print("从 flash_dump.bin 提取 app1 分区...")

# app1 分区信息（从分区表分析）
APP1_OFFSET = 0x650000
APP1_SIZE = 0x640000

try:
    with open('flash_dump.bin', 'rb') as f:
        f.seek(APP1_OFFSET)
        app1_data = f.read(APP1_SIZE)
    
    if len(app1_data) != APP1_SIZE:
        print(f"错误：只读取了 {len(app1_data)} 字节，期望 {APP1_SIZE}")
        sys.exit(1)
    
    with open('app1_backup.bin', 'wb') as f:
        f.write(app1_data)
    
    print(f"✓ 成功提取 app1 分区")
    print(f"  偏移: 0x{APP1_OFFSET:08X}")
    print(f"  大小: {APP1_SIZE} 字节 ({APP1_SIZE//1024} KB)")
    print(f"  保存为: app1_backup.bin")
    print()
    print("恢复命令:")
    print(f"  esptool -p COM5 --chip esp32c3 write_flash 0x{APP1_OFFSET:X} app1_backup.bin")
    
except FileNotFoundError:
    print("错误：找不到 flash_dump.bin")
    sys.exit(1)
except Exception as e:
    print(f"错误：{e}")
    sys.exit(1)
