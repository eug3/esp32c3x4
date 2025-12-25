<#
  erase_and_flash.ps1
  说明：备份现有 flash（可选）、擦除整片 flash，并刷入指定的 bin 文件。

  使用前准备：
  - 确保设备通过串口连接（例如 COM5），并且已在 `boot` 模式或可被 esptool 访问。
  - 在本工程的 Python 虚拟环境中已安装 `esptool`（可通过 `pip install esptool` 安装）。
  - 将要刷写的固件放在仓库根目录，命名为 `display_demo.bin`，或者使用 --bin 参数指定。

  这是一个破坏性操作：会清除整片 Flash。运行前请备份重要数据。
#>

param(
  [string]$Port = 'COM5',
  [string]$Bin = 'display_demo.bin',
  [int]$Baud = 2000000,
  [switch]$SkipBackup
)

$py = Join-Path -Path $PSScriptRoot -ChildPath 'esp32_env\Scripts\python.exe'
if (-not (Test-Path $py)) { $py = 'python' }

Write-Host "目标串口: $Port" -ForegroundColor Cyan
Write-Host "固件: $Bin" -ForegroundColor Cyan

if (-not $SkipBackup) {
  $backup = Join-Path $PSScriptRoot "flash_backup_$(Get-Date -Format yyyyMMdd_HHmmss).bin"
  Write-Host "开始备份现有 flash 到: $backup" -ForegroundColor Yellow
  & $py -m esptool --chip esp32c3 --port $Port read_flash 0x0 0x100000 $backup
  if ($LASTEXITCODE -ne 0) { Write-Error "备份失败，停止操作"; exit 1 }
}

Read-Host -Prompt "确认将擦除整片 flash 并刷写 $Bin？输入 Y 确认"

Write-Host "开始擦除 flash..." -ForegroundColor Yellow
& $py -m esptool --chip esp32c3 --port $Port erase_flash
if ($LASTEXITCODE -ne 0) { Write-Error "擦除失败"; exit 1 }

Write-Host "开始刷写 $Bin 到 0x0（波特率 $Baud）" -ForegroundColor Yellow
& $py -m esptool --chip esp32c3 --port $Port --baud $Baud write_flash -z 0x0 $Bin
if ($LASTEXITCODE -ne 0) { Write-Error "刷写失败"; exit 1 }

Write-Host "刷写完成。请复位设备并观察串口/显示输出。" -ForegroundColor Green
