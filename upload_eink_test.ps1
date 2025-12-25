#!/usr/bin/env pwsh

Write-Host "===== 编译并上传墨水屏GPIO测试程序 =====" -ForegroundColor Cyan

# 关闭可能占用串口的进程
Get-Process | Where-Object { $_.ProcessName -like '*python*' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# 使用 Arduino CLI 编译
$sketchPath = "d:\esp32c3x4\eink_driver_template"
$buildPath = "$sketchPath\build"

Write-Host "`n编译程序..." -ForegroundColor Yellow

# 检查是否有 arduino-cli
$arduinoCli = Get-Command arduino-cli -ErrorAction SilentlyContinue
if (-not $arduinoCli) {
    # 尝试从之前的构建中找到编译器
    Write-Host "未找到 arduino-cli，尝试手动编译..." -ForegroundColor Yellow
    
    # 复制之前的构建配置
    Copy-Item -Path "d:\esp32c3x4\test_app_xteink_v2\eink_driver_template.ino" -Destination $sketchPath -ErrorAction SilentlyContinue
    
    Write-Host "请在 Arduino IDE 中打开以下文件并编译上传:" -ForegroundColor Green
    Write-Host "$sketchPath\eink_driver_template.ino" -ForegroundColor Cyan
    Write-Host "`n或者按 Ctrl+C 取消，我会创建一个简化版本使用 esptool 上传" -ForegroundColor Yellow
    
    pause
    exit 1
}

# 编译
& $arduinoCli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc $sketchPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n✗ 编译失败" -ForegroundColor Red
    exit 1
}

Write-Host "`n✓ 编译成功" -ForegroundColor Green

# 上传
Write-Host "`n上传到设备..." -ForegroundColor Yellow
& $arduinoCli upload -p COM5 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc $sketchPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n✗ 上传失败" -ForegroundColor Red
    exit 1
}

Write-Host "`n✓ 上传成功" -ForegroundColor Green

# 等待设备重启
Start-Sleep -Seconds 3

# 打开串口监视器
Write-Host "`n打开串口监视器..." -ForegroundColor Yellow
& "d:\esp32c3x4\esp32_env\Scripts\python.exe" -m serial.tools.miniterm COM5 115200
