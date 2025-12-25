#!/usr/bin/env pwsh

Write-Host "===== 编译并上传 GPIO 测试程序 =====" -ForegroundColor Cyan

# 设置路径
$projectPath = "d:\esp32c3x4\test_app_xteink_v2"
$buildPath = "$projectPath\build\esp32.esp32.esp32c3"

# 检查是否已编译
if (-not (Test-Path "$buildPath\test_app_xteink_v2.ino.bin")) {
    Write-Host "`n需要先在 Arduino IDE 中编译项目" -ForegroundColor Yellow
    Write-Host "请执行以下步骤:" -ForegroundColor Green
    Write-Host "  1. 打开 Arduino IDE" -ForegroundColor White
    Write-Host "  2. 文件 -> 打开: $projectPath\test_app_xteink_v2.ino" -ForegroundColor White
    Write-Host "  3. 工具 -> 开发板: ESP32C3 Dev Module" -ForegroundColor White
    Write-Host "  4. 工具 -> USB CDC On Boot: Enabled" -ForegroundColor White
    Write-Host "  5. 项目 -> 编译 (Ctrl+R)" -ForegroundColor White
    Write-Host "`n编译完成后再次运行此脚本上传`n" -ForegroundColor Yellow
    pause
    exit 1
}

Write-Host "`n✓ 找到编译文件" -ForegroundColor Green

# 关闭占用串口的进程
Write-Host "`n关闭占用串口的进程..." -ForegroundColor Yellow
Get-Process | Where-Object { $_.ProcessName -like '*python*' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# 上传到 app1 分区
Write-Host "`n上传到 app1 分区 (0x650000)..." -ForegroundColor Yellow

& "d:\esp32c3x4\esp32_env\Scripts\python.exe" -m esptool `
    --chip esp32c3 `
    --port COM5 `
    --baud 921600 `
    --before default_reset `
    --after hard_reset `
    write_flash `
    -z `
    --flash_mode dio `
    --flash_freq 80m `
    --flash_size 16MB `
    0x650000 "$buildPath\test_app_xteink_v2.ino.bin"

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n✗ 上传失败" -ForegroundColor Red
    pause
    exit 1
}

Write-Host "`n✓ 上传成功" -ForegroundColor Green

# 设置 OTA 启动 app1
Write-Host "`n设置从 app1 启动..." -ForegroundColor Yellow

& "d:\esp32c3x4\esp32_env\Scripts\python.exe" -m esptool `
    --chip esp32c3 `
    --port COM5 `
    --baud 921600 `
    write_flash `
    0xE000 "d:\esp32c3x4\otadata_boot_app1.bin"

Write-Host "`n✓ OTA 配置完成" -ForegroundColor Green

# 等待设备重启
Write-Host "`n等待设备重启..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

# 打开串口监视器
Write-Host "`n打开串口监视器查看 GPIO 测试结果...`n" -ForegroundColor Cyan
& "d:\esp32c3x4\esp32_env\Scripts\python.exe" -m serial.tools.miniterm COM5 115200
