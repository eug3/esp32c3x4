import serial
import time

print('重启设备并捕获启动日志...\n')
print('='*70)

try:
    ser = serial.Serial('COM5', 115200, timeout=1)
    
    # 重启设备
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    
    print('设备重启中...\n')
    time.sleep(1)
    
    # 捕获启动日志
    start = time.time()
    found_app_info = False
    
    while time.time() - start < 8:
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    
                    # 检测关键信息
                    if 'XTEINK X4' in line or '自定义应用' in line:
                        print('\n>>> 检测到自定义应用！<<<\n')
                        found_app_info = True
                    elif 'app1' in line.lower() or '0x650000' in line or '0x00650000' in line:
                        print('\n>>> 从 app1 分区启动！<<<\n')
                        found_app_info = True
            except:
                pass
    
    print('='*70)
    
    if found_app_info:
        print('\n✓ 自定义应用正在运行')
        print('\n说明:')
        print('  - 串口输出正常（你看到了自定义应用的启动信息）')
        print('  - 墨水屏显示原厂内容是正常的（自定义应用没有墨水屏驱动）')
        print('  - 如需控制墨水屏，需要逆向原厂固件提取驱动代码')
    else:
        print('\n⚠ 未检测到自定义应用标识')
        print('  请查看上面的启动日志判断运行状态')
    
    ser.close()
    
except Exception as e:
    print(f'错误: {e}')
