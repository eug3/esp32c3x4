# XTEINK X4 应用切换脚本
# 安全方案：通过读取原厂 otadata 并修改来切换启动分区

$python = "d:\esp32c3x4\esp32_env\Scripts\python.exe"

Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host "XTEINK X4 应用切换工具" -ForegroundColor Cyan
Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host ""

Write-Host "当前状态：" -ForegroundColor Yellow
Write-Host "  app0 (0x010000): 原厂固件"
Write-Host "  app1 (0x650000): 自定义应用 v2"
Write-Host ""

Write-Host "选择启动应用：" -ForegroundColor Cyan
Write-Host "  [0] 原厂固件 (app0) - 默认"
Write-Host "  [1] 自定义应用 (app1)"
Write-Host "  [R] 读取 flash_dump 中的 otadata"
Write-Host ""

$choice = Read-Host "请选择 (0/1/R)"

switch ($choice) {
    "0" {
        Write-Host "`n提示：原厂固件已是默认启动" -ForegroundColor Green
        Write-Host "如果当前在 app1，重启即可回到 app0"
    }
    
    "1" {
        Write-Host "`n⚠ 警告：需要手动切换到 app1" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "方法 1: 通过串口（推荐）" -ForegroundColor Cyan
        Write-Host "  1. 连接串口监视器: $python -m serial.tools.miniterm COM5 115200"
        Write-Host "  2. 原厂固件添加代码在启动时检查某个GPIO"
        Write-Host "  3. 或者创建一个中间 bootloader"
        Write-Host ""
        Write-Host "方法 2: 修改原厂固件（需要IDA逆向）"Write-Host "  1. 分析 app0.bin 的启动逻辑"
        Write-Host "  2. 找到跳转到 app1 的代码位置"
        Write-Host "  3. 修改 bootloader 或者使用 OTA 机制"
        Write-Host ""
        Write-Host "方法 3: 暂时替换 app0（危险）" -ForegroundColor Red
        Write-Host "  烧录自定义应用到 app0 位置："
        Write-Host "  $python -m esptool -p COM5 --chip esp32c3 write_flash 0x10000 d:\esp32c3x4\test_app_xteink_v2\build\esp32.esp32.esp32c3\test_app_xteink_v2.ino.bin"
        Write-Host "  ⚠ 会丢失原厂固件！需要先备份 app0"
    }
    
    "R" {
        Write-Host "`n读取 flash_dump.bin 中 0xE000 的数据..." -ForegroundColor Cyan
        & $python -c @"
data = open('d:/esp32c3x4/flash_dump.bin', 'rb').read()
otadata = data[0xE000:0xE000+64]
print('OTA Data at 0xE000:')
print(otadata.hex())
print('\n前32字节:', ' '.join(f'{b:02x}' for b in otadata[:32]))
print('后32字节:', ' '.join(f'{b:02x}' for b in otadata[32:]))
"@
    }
    
    default {
        Write-Host "无效选择" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=" * 60 -ForegroundColor Cyan
