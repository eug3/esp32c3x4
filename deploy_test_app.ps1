# 部署测试应用到 XTEINK X4
# 用法: .\deploy_test_app.ps1 <编译输出的bin文件路径>

param(
    [string]$BinPath
)

Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host "XTEINK X4 测试应用 - 部署到设备" -ForegroundColor Cyan
Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host ""

# 检查参数
if (-not $BinPath) {
    Write-Host "错误: 请提供编译输出的 .bin 文件路径" -ForegroundColor Red
    Write-Host ""
    Write-Host "用法:" -ForegroundColor Yellow
    Write-Host "  .\deploy_test_app.ps1 `"C:\Users\...\test_app_xteink.ino.bin`""
    Write-Host ""
    Write-Host "提示: 在 Arduino IDE 编译后，从输出窗口复制 .bin 文件路径"
    exit 1
}

# 检查文件是否存在
if (-not (Test-Path $BinPath)) {
    Write-Host "错误: 找不到文件: $BinPath" -ForegroundColor Red
    exit 1
}

$fileSize = (Get-Item $BinPath).Length
Write-Host "✓ 找到编译输出: $BinPath" -ForegroundColor Green
Write-Host "  文件大小: $($fileSize / 1024) KB" -ForegroundColor Gray
Write-Host ""

# 检查文件大小 (app1 分区最大 6.4MB)
$maxSize = 6400 * 1024
if ($fileSize -gt $maxSize) {
    Write-Host "警告: 文件大小 ($($fileSize / 1024 / 1024) MB) 超过 app1 分区限制 (6.4 MB)" -ForegroundColor Red
    $continue = Read-Host "是否继续? (y/N)"
    if ($continue -ne 'y') {
        exit 1
    }
}

# Python 路径
$python = "d:\esp32c3x4\esp32_env\Scripts\python.exe"

Write-Host "部署计划:" -ForegroundColor Cyan
Write-Host "  1. 烧录测试应用到 app1 分区 (0x650000)"
Write-Host "  2. 设置 OTA 启动到 app1"
Write-Host "  3. 重启设备"
Write-Host ""

$confirm = Read-Host "确认开始部署? (Y/n)"
if ($confirm -eq 'n') {
    Write-Host "已取消" -ForegroundColor Yellow
    exit 0
}

Write-Host ""
Write-Host "步骤 1/3: 烧录测试应用到 0x650000..." -ForegroundColor Yellow

& $python -m esptool -p COM5 --chip esp32c3 write_flash 0x650000 $BinPath

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "✗ 烧录失败!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "✓ 应用烧录成功" -ForegroundColor Green
Write-Host ""
Write-Host "步骤 2/3: 设置启动到 app1..." -ForegroundColor Yellow

& $python -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_select_ota_1.bin

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "✗ OTA 设置失败!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "✓ OTA 启动配置成功" -ForegroundColor Green
Write-Host ""
Write-Host "步骤 3/3: 重启设备..." -ForegroundColor Yellow

& $python -m esptool -p COM5 --chip esp32c3 run

Write-Host ""
Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host "✓ 部署完成!" -ForegroundColor Green
Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host ""
Write-Host "查看串口输出:" -ForegroundColor Cyan
Write-Host "  $python -m serial.tools.miniterm COM5 115200" -ForegroundColor Gray
Write-Host ""
Write-Host "预期输出:" -ForegroundColor Cyan
Write-Host "  ========================================"
Write-Host "    XTEINK X4 自定义应用"
Write-Host "  ========================================"
Write-Host "  运行分区: app1"
Write-Host "  地址: 0x00650000"
Write-Host "  ..."
Write-Host ""
Write-Host "恢复到原厂固件:" -ForegroundColor Yellow
Write-Host "  $python -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_select_ota_0.bin"
Write-Host ""

# 询问是否打开串口监视器
$openSerial = Read-Host "是否打开串口监视器查看输出? (Y/n)"
if ($openSerial -ne 'n') {
    Write-Host ""
    Write-Host "提示: 按 Ctrl+] 退出串口监视器" -ForegroundColor Yellow
    Start-Sleep -Seconds 2
    & $python -m serial.tools.miniterm COM5 115200
}
