#!/usr/bin/env python3
"""快速验证自定义应用是否运行"""

import serial
import time
import sys

print("=" * 60)
print("XTEINK X4 应用验证工具")
print("=" * 60)
print()

try:
    print("连接串口 COM5...")
    ser = serial.Serial('COM5', 115200, timeout=2)
    time.sleep(0.5)
    
    print("读取设备输出 (5秒)...\n")
    print("-" * 60)
    
    start_time = time.time()
    found_custom_app = False
    
    while time.time() - start_time < 5:
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    
                    # 检测关键字
                    if 'XTEINK X4' in line or 'app1' in line or '0x00650000' in line:
                        found_custom_app = True
                    
            except Exception as e:
                pass
        time.sleep(0.1)
    
    print("-" * 60)
    print()
    
    if found_custom_app:
        print("✓ 自定义应用正在运行！")
    else:
        print("⚠ 未检测到自定义应用输出")
        print("  可能原因:")
        print("  1. 应用已启动完成，无新输出")
        print("  2. 应用未正常运行")
        print("  3. 串口波特率不匹配")
    
    ser.close()
    
except serial.SerialException as e:
    print(f"✗ 串口错误: {e}")
    print("\n提示: 关闭其他占用 COM5 的程序")
    sys.exit(1)
except Exception as e:
    print(f"✗ 错误: {e}")
    sys.exit(1)

print()
print("手动查看完整输出:")
print("  d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m serial.tools.miniterm COM5 115200")
