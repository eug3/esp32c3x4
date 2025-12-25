#!/usr/bin/env python3
"""完整的设备状态检查和诊断"""

import serial
import time
import struct

print("=" * 70)
print("XTEINK X4 设备状态完整检查")
print("=" * 70)
print()

# 1. 读取 OTA 设置
print("1. 检查 OTA 配置...")
try:
    with open('otadata_verify.bin', 'rb') as f:
        data = f.read()
    
    seq = struct.unpack('<I', data[0:4])[0]
    ota_seq = struct.unpack('<I', data[4:8])[0]
    crc = struct.unpack('<I', data[8:12])[0]
    
    print(f"   seq = {seq}")
    print(f"   ota_seq = {ota_seq}")
    print(f"   crc = 0x{crc:08X}")
    
    if ota_seq == 1:
        print("   ✓ OTA 设置: app1 (自定义应用)")
    elif ota_seq == 0:
        print("   ⚠ OTA 设置: app0 (原厂固件)")
    else:
        print("   ✗ OTA 未设置")
except Exception as e:
    print(f"   ✗ 无法读取 OTA 配置: {e}")

print()

# 2. 连接串口并监听
print("2. 连接串口 COM5...")
try:
    ser = serial.Serial('COM5', 115200, timeout=1)
    print("   ✓ 串口已连接")
    print()
    
    # 清空缓冲区
    ser.reset_input_buffer()
    
    print("3. 监听设备输出 (15秒)...")
    print("   " + "-" * 66)
    
    start_time = time.time()
    line_count = 0
    keywords_found = []
    
    while time.time() - start_time < 15:
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    line_count += 1
                    print(f"   {line}")
                    
                    # 检测关键字
                    if 'XTEINK' in line or '自定义' in line:
                        keywords_found.append('自定义应用标题')
                    if 'app1' in line.lower() or '0x650000' in line or '0x00650000' in line:
                        keywords_found.append('app1 分区')
                    if '运行时间' in line or '心跳' in line or '❤' in line:
                        keywords_found.append('心跳输出')
                    if 'error' in line.lower() or 'failed' in line.lower():
                        keywords_found.append('错误信息')
                    
            except Exception as e:
                pass
        else:
            time.sleep(0.1)
    
    print("   " + "-" * 66)
    print()
    
    # 分析结果
    print("4. 分析结果:")
    print(f"   接收到 {line_count} 行输出")
    
    if keywords_found:
        print("   检测到关键字:")
        for kw in set(keywords_found):
            print(f"     - {kw}")
    
    print()
    
    if '自定义应用标题' in keywords_found:
        print("   ✓✓✓ 自定义应用正在运行！")
    elif '心跳输出' in keywords_found:
        print("   ✓ 应用正在运行（检测到心跳）")
    elif 'app1 分区' in keywords_found:
        print("   ✓ 设备从 app1 启动")
    elif line_count == 0:
        print("   ⚠ 无输出 - 可能原因:")
        print("     1. 应用已启动完成，当前无输出")
        print("     2. 应用正在运行但无串口输出")
        print("     3. 设备处于待机状态")
        print()
        print("   建议: 手动重启设备查看启动日志")
        print("   命令: d:\\esp32c3x4\\esp32_env\\Scripts\\python.exe -m esptool -p COM5 --chip esp32c3 run")
    else:
        print(f"   有输出但未检测到自定义应用关键字")
        print("   请检查上面的输出内容")
    
    ser.close()
    
except serial.SerialException as e:
    print(f"   ✗ 串口错误: {e}")
    print()
    print("   可能原因:")
    print("   - 其他程序占用 COM5")
    print("   - 设备未连接")
    print("   - 驱动问题")

print()
print("=" * 70)
