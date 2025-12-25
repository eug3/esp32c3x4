# XTEINK X4 完整测试脚本
# 测试自定义应用启动

$python = "d:\esp32c3x4\esp32_env\Scripts\python.exe"

Write-Host "=" * 70 -ForegroundColor Cyan
Write-Host "XTEINK X4 自定义应用测试" -ForegroundColor Cyan
Write-Host "=" * 70 -ForegroundColor Cyan
Write-Host ""

Write-Host "当前状态:" -ForegroundColor Yellow
Write-Host "  app1 (0x650000): 自定义应用 v2 已烧录"
Write-Host "  otadata (0xE000): 应该已设置启动到 app1"
Write-Host ""

$choice = Read-Host "选择操作 [1=查看启动日志, 2=切换到app0, 3=切换到app1, 4=读取otadata状态]"

switch ($choice) {
    "1" {
        Write-Host "`n连接串口监视器 (Ctrl+] 退出)..." -ForegroundColor Cyan
        Start-Sleep -Seconds 1
        & $python -m serial.tools.miniterm COM5 115200
    }
    
    "2" {
        Write-Host "`n切换到 app0 (原厂固件)..." -ForegroundColor Yellow
        & $python -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_boot_app0.bin
        
        Write-Host "`n重启设备..." -ForegroundColor Cyan
        Start-Sleep -Seconds 2
        & $python -m esptool -p COM5 --chip esp32c3 run
        
        Write-Host "`n设备已重启到 app0"
    }
    
    "3" {
        Write-Host "`n切换到 app1 (自定义应用)..." -ForegroundColor Yellow
        & $python -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_boot_app1.bin
        
        Write-Host "`n重启设备..." -ForegroundColor Cyan
        Start-Sleep -Seconds 2
        & $python -m esptool -p COM5 --chip esp32c3 run
        
        Write-Host "`n设备已重启到 app1"
    }
    
    "4" {
        Write-Host "`n读取设备当前 otadata 状态..." -ForegroundColor Cyan
        
        # 读取 otadata
        & $python -m esptool -p COM5 --chip esp32c3 read_flash 0xE000 0x2000 otadata_current.bin
        
        Write-Host "`n解析 otadata..." -ForegroundColor Yellow
        & $python -c @"
import struct

with open('otadata_current.bin', 'rb') as f:
    data = f.read()

print('OTA Data (0xE000):')
print('  前16字节:', data[:16].hex())

seq0 = struct.unpack('<I', data[0:4])[0]
ota_seq0 = struct.unpack('<I', data[4:8])[0]
crc0 = struct.unpack('<I', data[8:12])[0]

seq1 = struct.unpack('<I', data[4096:4100])[0]
ota_seq1 = struct.unpack('<I', data[4100:4104])[0]

print(f'  Entry 0: seq={seq0}, ota_seq={ota_seq0}, crc={crc0:08x}')
print(f'  Entry 1: seq={seq1}, ota_seq={ota_seq1}')

if ota_seq0 == 0:
    print('\n  -> 当前启动: app0 (ota_0)')
elif ota_seq0 == 1:
    print('\n  -> 当前启动: app1 (ota_1)')
elif ota_seq0 == 0xFFFFFFFF:
    print('\n  -> 未设置OTA，默认启动 app0')
"@
    }
    
    default {
        Write-Host "无效选择" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=" * 70 -ForegroundColor Cyan
