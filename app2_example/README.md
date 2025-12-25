app2_example — 最小示例工程

目的：生成一个最小可运行的 `app2` 示例，并展示如何在运行时将下次启动设置为 `app2`（通过 UART 输入 `2`）。

文件结构：
- `CMakeLists.txt` — 顶层 CMake
- `main/CMakeLists.txt` — 组件 CMake
- `main/main.c` — 示例代码，包含 `boot_to_app2()` 调用
- `partitions.csv` — 分区表（含 `app2`）

构建与刷写（假设已正确安装并配置 ESP-IDF）：

1. 导出 IDF 环境并进入工程目录：

```powershell
# Windows Powershell 示例（请改为你的 IDF 环境脚本路径）
# PowerShell: 执行 idf 环境激活脚本，例如：
# & 'C:\path\to\esp-idf\export.ps1'

cd d:\esp32c3x4\app2_example
```

2. 配置项目使用自定义分区表：

```powershell
idf.py menuconfig
# 在 'Partition Table' 选择 'Custom partition table CSV' 并设置为
# ${PROJECT_DIR}/partitions.csv
```

3. 构建：

```powershell
idf.py build
```

4. 刷写（仅写 app2 到相应偏移，避免覆盖现有 app0/app1）：

```powershell
# 假设 build 输出在 build目录，且 app2 的固件名为 app2_example.bin
python -m esptool --chip esp32c3 --port COMx write_flash 0xC90000 build/app2_example.bin
```

或使用 idf.py flash（若 partition-table/bootloader 也需要写入，请用 idf.py flash）：

```powershell
idf.py -p COMx flash
```

如何使用：
- 刷入 `app2` 后，运行当前任何 app（例如 app0），通过 UART 发送字符 `2`，示例程序会调用 `esp_ota_set_boot_partition()` 将下次启动设置为 `app2` 并重启。

注意：
- 如果不想覆盖现有的 partition table，请只写入 `app2` 固件到 `0xC90000`。
- 确认 `app2` 大小在 `0x370000` 范围内。若更大，请调整 `partitions.csv`。
