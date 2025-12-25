# ESP32-C3 启动选择器

一个智能启动选择工具，允许在 ESP32-C3 启动时选择要运行的应用程序（app0、app1 或 app2）。

## 功能特性

- 🎯 启动时交互式菜单（通过串口）
- ⏱️ 10秒倒计时自动启动默认应用
- 💾 支持保存默认启动应用（存储在 NVS）
- 🔘 GPIO 触发强制进入选择菜单（GPIO9，可修改）
- 📊 显示每个应用分区的状态和大小

## 使用方法

### 1. 构建与刷写

```powershell
# 进入工程目录
cd d:\esp32c3x4\boot_selector

# 设置目标芯片
idf.py set-target esp32c3

# 配置项目（可选，使用默认配置）
idf.py menuconfig

# 构建
idf.py build

# 刷写（替换 COMx 为实际串口）
idf.py -p COMx flash monitor
```

### 2. 启动选择

设备启动后会显示菜单：

```
╔════════════════════════════════════════╗
║     ESP32-C3 启动选择器 v1.0          ║
╚════════════════════════════════════════╝

请选择要启动的应用程序：

  [0] app0 (已找到，大小: 0x640000)
  [1] app1 (已找到，大小: 0x640000)
  [2] app2 (已找到，大小: 0x370000)

  [r] 重新显示菜单
  [s] 保存选择并设为默认

倒计时 10 秒后将启动默认应用...
请按数字键选择:
```

### 3. 操作说明

- 按 `0`、`1` 或 `2` 选择对应应用
- 按回车键启动选定的应用
- 按 `s` 键保存当前选择为默认并启动
- 按 `r` 键重新显示菜单并重置倒计时
- 不操作则10秒后自动启动默认应用

### 4. GPIO 强制选择模式

- 将 GPIO9 拉低（接地）后启动设备
- 设备会强制进入选择菜单，忽略默认启动应用
- 适用于开发调试或紧急切换

## 分区表说明

使用 `partitions.csv`，支持 16MB flash：

- **app0**: 0x10000 - 0x64FFFF (6.25MB)
- **app1**: 0x650000 - 0xC8FFFF (6.25MB)
- **app2**: 0xC90000 - 0xFFFFFF (~3.4MB)

## 刷写完整系统

如果需要刷写 boot_selector 和所有应用：

```powershell
# 使用 esptool 刷写所有分区
python -m esptool --chip esp32c3 --port COMx write_flash -z ^
  0x0 build/bootloader/bootloader.bin ^
  0x8000 build/partition_table/partition-table.bin ^
  0x10000 path/to/app0.bin ^
  0x650000 path/to/app1.bin ^
  0xC90000 path/to/app2.bin
```

## 配置选项

### 修改 GPIO 选择引脚

编辑 `main/main.c`，修改：

```c
#define GPIO_BOOT_SELECT GPIO_NUM_9  // 改为你需要的 GPIO
```

### 修改超时时间

编辑 `main/main.c`，修改：

```c
#define TIMEOUT_SECONDS 10  // 改为你需要的秒数
```

## 工作原理

1. **启动检测**：检查 GPIO 状态和 NVS 中保存的默认应用
2. **菜单显示**：通过串口显示可用应用列表
3. **用户选择**：等待用户输入或倒计时结束
4. **分区切换**：调用 `esp_ota_set_boot_partition()` 设置启动分区
5. **重启设备**：`esp_restart()` 重启到选定应用

## 注意事项

- ⚠️ boot_selector 本身也是一个应用，建议刷写到 app0 分区
- ⚠️ 确保所有应用都使用相同的分区表
- ⚠️ GPIO 选择引脚请避开 strapping pins（GPIO2, GPIO8, GPIO9）
- ⚠️ 首次使用建议先刷写完整系统（bootloader + partition table + apps）

## 故障排除

### 无法找到分区
- 检查分区表是否正确刷写到 0x8000
- 确认应用固件已刷写到对应偏移地址

### GPIO 触发无效
- 检查 GPIO 引脚配置是否正确
- 确认硬件连接（GPIO 接地时应触发）

### 切换后无法启动
- 检查目标应用是否有效
- 确认应用大小未超过分区限制
