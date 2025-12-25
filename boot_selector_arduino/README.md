# Arduino 启动选择器

Arduino 版本的 ESP32-C3 启动选择器，无需 ESP-IDF。

## 使用 Arduino IDE 编译上传

### 1. 打开项目
- 在 Arduino IDE 中打开 `boot_selector_arduino.ino`

### 2. 配置开发板
- **开发板**: ESP32C3 Dev Module
- **USB CDC On Boot**: Enabled
- **CPU Frequency**: 160MHz
- **Flash Size**: 16MB
- **Partition Scheme**: Minimal SPIFFS (建议) 或 Default 4MB with spiffs

### 3. 上传
- 连接 ESP32-C3 到电脑
- 选择正确的串口 (COM5)
- 点击上传

## 使用 Arduino CLI 编译上传

如果你安装了 Arduino CLI，可以使用命令行：

```powershell
# 编译
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs,FlashSize=16M boot_selector_arduino

# 上传到 COM5
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32c3 boot_selector_arduino

# 或者一步完成
arduino-cli compile --upload -p COM5 --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs,FlashSize=16M boot_selector_arduino
```

## 功能特性

与 ESP-IDF 版本相同：
- ✅ 启动时交互式菜单
- ✅ 10秒倒计时自动启动
- ✅ NVS 保存默认应用
- ✅ GPIO9 强制进入菜单
- ✅ 支持 app0/app1/app2

## 注意事项

1. **分区表兼容性**
   - Arduino 默认分区表可能与自定义的不同
   - 建议使用 "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)" 或自定义分区表

2. **自定义分区表（高级）**
   - 将 `partitions.csv` 复制到 Arduino ESP32 核心目录
   - 路径示例: `C:\Users\[用户名]\AppData\Local\Arduino15\packages\esp32\hardware\esp32\[版本]\tools\partitions\`
   - 在 Arduino IDE 中选择自定义分区表

3. **烧录位置**
   - 此程序应烧录到 app0 (0x10000) 位置
   - app1 和 app2 保持在原位置

## 快速测试

1. 上传此程序到 ESP32-C3
2. 打开串口监视器 (115200 波特率)
3. 重启设备，会看到启动菜单
4. 按 0/1/2 选择要启动的应用

## 问题排查

**串口无输出**
- 确认 "USB CDC On Boot" 设为 Enabled
- 使用正确的波特率 (115200)

**找不到分区**
- 检查分区表是否正确烧录
- 确认 app0.bin 和 app1.bin 在正确位置

**无法切换应用**
- 确认目标应用已烧录且有效
- 检查 NVS 分区是否正常
