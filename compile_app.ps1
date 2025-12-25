# Arduino 编译和烧录脚本
# 用于 XTEINK X4 测试应用

Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host "XTEINK X4 测试应用 - 编译和烧录" -ForegroundColor Cyan
Write-Host "=" * 60 -ForegroundColor Cyan
Write-Host ""

# 查找 Arduino IDE
$arduinoPaths = @(
    "$env:LOCALAPPDATA\Programs\Arduino IDE\Arduino IDE.exe",
    "$env:ProgramFiles\Arduino IDE\Arduino IDE.exe",
    "${env:ProgramFiles(x86)}\Arduino IDE\Arduino IDE.exe",
    "$env:LOCALAPPDATA\Arduino15\arduino-cli.exe"
)

$arduinoPath = $null
foreach ($path in $arduinoPaths) {
    if (Test-Path $path) {
        $arduinoPath = $path
        Write-Host "✓ 找到 Arduino: $path" -ForegroundColor Green
        break
    }
}

if (-not $arduinoPath) {
    Write-Host "✗ 未找到 Arduino IDE" -ForegroundColor Red
    Write-Host ""
    Write-Host "请在 Arduino IDE 中手动编译:" -ForegroundColor Yellow
    Write-Host "1. 打开: d:\esp32c3x4\test_app_xteink\test_app_xteink.ino"
    Write-Host "2. 工具 -> 开发板 -> ESP32C3 Dev Module"
    Write-Host "3. 工具 -> Flash Size -> 16MB"
    Write-Host "4. 工具 -> Partition Scheme -> Default 4MB with spiffs"
    Write-Host "5. 项目 -> 验证/编译 (Ctrl+R)"
    Write-Host "6. 查看输出窗口，找到 .bin 文件路径"
    Write-Host ""
    Write-Host "然后运行: .\deploy_test_app.ps1 <bin文件路径>" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "提示: Arduino IDE 2.x 需要手动编译" -ForegroundColor Yellow
Write-Host ""
Write-Host "请按照以下步骤操作:" -ForegroundColor Cyan
Write-Host "1. 打开 Arduino IDE"
Write-Host "2. 文件 -> 打开 -> d:\esp32c3x4\test_app_xteink\test_app_xteink.ino"
Write-Host ""
Write-Host "3. 配置开发板 (工具菜单):"
Write-Host "   - 开发板: ESP32C3 Dev Module"
Write-Host "   - Flash Size: 16MB (128Mb)"
Write-Host "   - Partition Scheme: Default 4MB with spiffs"
Write-Host "   - Upload Speed: 921600"
Write-Host "   - Port: COM5"
Write-Host ""
Write-Host "4. 编译: 项目 -> 验证/编译 (或 Ctrl+R)"
Write-Host ""
Write-Host "5. 查看输出窗口最后几行，找到类似这样的路径:"
Write-Host "   C:\Users\<用户>\AppData\Local\Temp\arduino\sketches\...\test_app_xteink.ino.bin"
Write-Host ""
Write-Host "6. 复制该路径，然后运行:"
Write-Host "   .\deploy_test_app.ps1 `"<复制的路径>`"" -ForegroundColor Green
Write-Host ""
Write-Host "=" * 60 -ForegroundColor Cyan

# 打开 Arduino IDE（如果找到了）
if ($arduinoPath -like "*.exe") {
    Write-Host ""
    $response = Read-Host "是否打开 Arduino IDE? (Y/n)"
    if ($response -ne 'n') {
        Start-Process $arduinoPath -ArgumentList "d:\esp32c3x4\test_app_xteink\test_app_xteink.ino"
        Write-Host "✓ 已打开 Arduino IDE" -ForegroundColor Green
    }
}
