# 刷写演示固件说明

目标：擦掉设备整片 flash，并刷入一个最小演示固件，使设备启动后可在墨水屏上显示内容（或用于后续调试）。

步骤概览：

1) 准备开发环境
- 在 `d:/esp32c3x4/esp32_env` 的虚拟环境中安装 `esptool`：

```powershell
.\esp32_env\Scripts\python.exe -m pip install esptool
```

2) 准备固件
- 将编译得到的 `display_demo.bin` 放到 `d:/esp32c3x4/`。如果你使用 Arduino IDE/PlatformIO，请先创建并编译 `display_demo/display_demo.ino`，并导出 bin 文件。

3) 备份与刷写（破坏性）
- 运行刷写脚本，默认使用 `COM5`，会提示确认：

```powershell
.\erase_and_flash.ps1 -Port COM5 -Bin display_demo.bin
```

参数说明：
- `-SkipBackup`：跳过备份步骤（如果确定无需备份）。
- `-Baud`：刷写波特率，默认 2000000。

4) 验证
- 刷写完成并复位后，打开串口（115200）观察日志；若固件中包含 GxEPD2 调用并已正确配置引脚/驱动，显示应更新。

注意事项：
- 本脚本假设芯片为 `esp32c3` 并使用 `esptool` 对应参数。若你的硬件为不同芯片，请修改脚本。
- 刷写前请务必备份原始 flash（脚本默认会备份）。
- 若使用不同串口，请在命令中用 `-Port` 覆盖。
