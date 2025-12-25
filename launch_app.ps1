# XTEINK X4 自定义应用一键启动脚本

$python = "d:\esp32c3x4\esp32_env\Scripts\python.exe"

Write-Host "=" * 70 -ForegroundColor Cyan
Write-Host "XTEINK X4 自定义应用 - 一键启动" -ForegroundColor Cyan
Write-Host "=" * 70 -ForegroundColor Cyan
Write-Host ""

# 1. 杀掉可能占用串口的进程
Write-Host "1. 清理后台进程..." -ForegroundColor Yellow
Get-Process | Where-Object {$_.ProcessName -like "*python*" -and $_.MainWindowTitle -eq ""} | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# 2. 烧录 OTA data
Write-Host "2. 设置启动到 app1..." -ForegroundColor Yellow
& $python -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_boot_app1.bin 2>&1 | Select-String -Pattern "(Wrote|Hash|Hard reset)" -CaseSensitive

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n✗ 烧录失败！" -ForegroundColor Red
    exit 1
}

Write-Host "`n✓ OTA 设置完成" -ForegroundColor Green

# 3. 等待设备重启
Write-Host "`n3. 等待设备重启..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

# 4. 重启设备
Write-Host "4. 手动重启设备..." -ForegroundColor Yellow
& $python -m esptool -p COM5 --chip esp32c3 run 2>&1 | Out-Null
Start-Sleep -Seconds 2

# 5. 连接串口
Write-Host "5. 连接串口监听输出..." -ForegroundColor Yellow
Write-Host ""
Write-Host "=" * 70 -ForegroundColor Green
Write-Host "开始监听串口 (按 Ctrl+] 退出)" -ForegroundColor Green
Write-Host "=" * 70 -ForegroundColor Green
Write-Host ""

Start-Sleep -Seconds 1

& $python -m serial.tools.miniterm COM5 115200
