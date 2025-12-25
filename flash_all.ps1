# 一键刷写脚本 - 刷写 boot_selector 和所有应用到 ESP32-C3
# 使用方法: .\flash_all.ps1 -Port COM3

param(
    [string]$Port = "COM3",
    [string]$Chip = "esp32c3"
)

$ErrorActionPreference = "Stop"

Write-Host "════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  ESP32-C3 完整系统刷写脚本" -ForegroundColor Cyan
Write-Host "════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# 检查文件是否存在
$files = @{
    "boot_selector" = "boot_selector\build\boot_selector.bin"
    "app0" = "app0.bin"
    "app1" = "app1.bin"
}

Write-Host "检查必需文件..." -ForegroundColor Yellow
foreach ($name in $files.Keys) {
    $path = $files[$name]
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        Write-Host "  ✓ $name`: $path ($([math]::Round($size/1024, 2)) KB)" -ForegroundColor Green
    } else {
        Write-Host "  ✗ $name`: $path (未找到)" -ForegroundColor Red
        Write-Host ""
        Write-Host "提示: boot_selector 需要先构建:" -ForegroundColor Yellow
        Write-Host "  cd boot_selector" -ForegroundColor Gray
        Write-Host "  idf.py build" -ForegroundColor Gray
        exit 1
    }
}

Write-Host ""
Write-Host "准备刷写到 $Port..." -ForegroundColor Yellow
Write-Host ""

# 构建刷写命令
$cmd = "python -m esptool --chip $Chip --port $Port write_flash -z"
$cmd += " 0x10000 boot_selector\build\boot_selector.bin"
$cmd += " 0x650000 app1.bin"
$cmd += " 0xC90000 app0.bin"  # 将 app0 作为 app2

Write-Host "执行命令:" -ForegroundColor Cyan
Write-Host $cmd -ForegroundColor Gray
Write-Host ""

# 执行刷写
Invoke-Expression $cmd

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "════════════════════════════════════════" -ForegroundColor Green
    Write-Host "  刷写完成！" -ForegroundColor Green
    Write-Host "════════════════════════════════════════" -ForegroundColor Green
    Write-Host ""
    Write-Host "分区布局:" -ForegroundColor Cyan
    Write-Host "  0x10000  - boot_selector (启动选择器)" -ForegroundColor White
    Write-Host "  0x650000 - app1" -ForegroundColor White
    Write-Host "  0xC90000 - app2 (使用 app0.bin)" -ForegroundColor White
    Write-Host ""
    Write-Host "下一步:" -ForegroundColor Yellow
    Write-Host "  1. 连接串口监视器: idf.py -p $Port monitor" -ForegroundColor Gray
    Write-Host "  2. 重启设备，在菜单中选择要启动的应用" -ForegroundColor Gray
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "刷写失败，错误代码: $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
