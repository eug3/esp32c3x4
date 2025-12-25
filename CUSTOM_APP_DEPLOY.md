# XTEINK X4 自定义应用部署方案

## 原厂分区布局

| 分区 | 类型 | 偏移 | 大小 | 用途 |
|------|------|------|------|------|
| nvs | data/nvs | 0x009000 | 20KB | 配置存储 |
| otadata | data/ota | 0x00E000 | 8KB | OTA 启动选择 |
| **app0** | app/ota_0 | 0x010000 | 6.4MB | 原厂固件槽 0 |
| **app1** | app/ota_1 | 0x650000 | 6.4MB | 原厂固件槽 1 |
| spiffs | data/spiffs | 0xC90000 | 3.4MB | 文件系统 |
| coredump | data/coredump | 0xFF0000 | 64KB | 崩溃转储 |

**总计：16MB Flash 完全使用**

---

## 方案选择

### ✅ 方案 1：替换 app1 为自定义应用（推荐）

**优点：**
- 不修改分区表，最安全
- app0 保留原厂固件，可随时切换回去
- 使用 OTA 机制正常启动

**步骤：**

1. **编译自定义应用**（必须 ≤ 6.4MB）
   ```bash
   # Arduino IDE 设置：
   # Board: ESP32C3 Dev Module
   # Flash Size: 16MB
   # Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
   ```

2. **烧录到 app1 分区**
   ```bash
   esptool -p COM5 --chip esp32c3 write_flash 0x650000 your_app.bin
   ```

3. **设置 OTA 启动到 app1**
   ```bash
   # 方法 A: 使用 esptool 写入 otadata
   # (创建一个 otadata.bin 指向 ota_1)
   
   # 方法 B: 在自定义 app 中调用
   esp_ota_set_boot_partition(esp_ota_get_partition_subtype(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1));
   ```

4. **重启即可运行自定义应用**

**恢复原厂：**
```bash
# 重新从 flash_dump.bin 恢复 app1
esptool -p COM5 --chip esp32c3 write_flash 0x650000 flash_dump_app1.bin

# 或者设置 otadata 回到 ota_0
```

---

### ⚠️ 方案 2：Boot Selector（高级）

如果需要启动时选择应用，可以创建一个微型 bootloader 替换 app0：

**限制：**
- 需要重新编译 bootloader
- 需要保留墨水屏驱动
- 风险较高

**步骤：**

1. **编译 boot_selector**（必须 < 100KB）
   - 只包含 GPIO 检测 + OTA 切换逻辑
   - 不初始化墨水屏
   - 启动时检测按钮，选择 app1/app2

2. **烧录 boot_selector 到 app0**
   ```bash
   esptool -p COM5 write_flash 0x10000 boot_selector.bin
   ```

3. **自定义应用烧录到 app1**
   ```bash
   esptool -p COM5 write_flash 0x650000 your_app.bin
   ```

**风险：** 如果 boot_selector 有问题，设备将无法启动

---

## 推荐实施步骤

### Step 1: 提取原厂 app1 备份
```bash
esptool -p COM5 --chip esp32c3 read_flash 0x650000 0x640000 app1_backup.bin
```

### Step 2: 创建简单测试应用

创建 `test_app/test_app.ino`:
```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n===================");
  Serial.println("自定义应用已启动！");
  Serial.println("===================");
  Serial.println("硬件: XTEINK X4");
  Serial.println("芯片: ESP32-C3");
  
  // 获取当前运行分区
  const esp_partition_t *running = esp_ota_get_running_partition();
  Serial.printf("运行分区: %s (0x%08X)\n", 
                running->label, running->address);
}

void loop() {
  Serial.println("心跳...");
  delay(1000);
}
```

### Step 3: 编译并烧录

**Arduino IDE 设置：**
- 开发板：ESP32C3 Dev Module
- Flash Size：16MB (128Mb)
- Partition Scheme：**Default 4MB with spiffs**
- Upload Speed：921600
- CPU Frequency：160MHz
- Core Debug Level：None

**编译后手动烧录：**
```bash
# 找到编译输出目录（通常在 C:\Users\<用户>\AppData\Local\Temp\arduino\sketches\）
# 找到 test_app.ino.bin

esptool -p COM5 --chip esp32c3 write_flash 0x650000 test_app.ino.bin
```

### Step 4: 设置启动到自定义应用

**方法 A: 创建 otadata 切换脚本**

`switch_to_app1.py`:
```python
#!/usr/bin/env python3
import struct

# OTA Data 格式 (32 bytes per entry, 2 entries total)
# 启动到 ota_1 (app1)
otadata = bytearray(64)

# Entry 0: ota_1 selected
otadata[0:4] = struct.pack('<I', 0x00000001)  # seq = 1
otadata[4:8] = struct.pack('<I', 0x00000001)  # ota_1
otadata[8:12] = struct.pack('<I', 0xFFFFFFFF) # crc32 placeholder

# Entry 1: invalid
otadata[32:36] = struct.pack('<I', 0xFFFFFFFF)

with open('otadata_select_app1.bin', 'wb') as f:
    f.write(otadata)

print("创建 otadata_select_app1.bin")
print("烧录命令: esptool -p COM5 write_flash 0xE000 otadata_select_app1.bin")
```

运行：
```bash
python switch_to_app1.py
esptool -p COM5 --chip esp32c3 write_flash 0xE000 otadata_select_app1.bin
```

**方法 B: 在应用内自动切换**

在 `test_app.ino` 的 `setup()` 开始添加：
```cpp
// 设置自己为默认启动应用
const esp_partition_t *app1 = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, 
    ESP_PARTITION_SUBTYPE_APP_OTA_1, 
    NULL
);
if (app1) {
    esp_ota_set_boot_partition(app1);
    Serial.println("已设置 app1 为默认启动分区");
}
```

### Step 5: 重启测试

```bash
esptool -p COM5 --chip esp32c3 run
```

或者直接重启设备。

---

## 恢复原厂固件

### 完整恢复
```bash
esptool -p COM5 --chip esp32c3 write_flash 0x0 flash_dump.bin
```

### 只恢复 app1
```bash
esptool -p COM5 write_flash 0x650000 app1_backup.bin
# 然后切换回 ota_0
```

---

## 注意事项

1. **墨水屏驱动**：原厂固件有专用墨水屏驱动，自定义应用如果要使用墨水屏需要：
   - 分析原厂固件的 GPIO 配置
   - 或使用通用 e-ink 库（可能不兼容）

2. **电源管理**：ESP32-C3 的深度睡眠配置可能影响续航

3. **备份重要**：每次修改前都要备份当前分区

4. **分区大小**：自定义应用必须 ≤ 6.4MB

5. **调试**：保持串口连接（COM5, 115200）查看启动日志

---

## 下一步

选择方案后，我可以帮你：
- [ ] 提取 app1 备份
- [ ] 创建测试应用
- [ ] 生成 OTA 切换脚本
- [ ] 烧录和测试

请告诉我你想使用哪个方案？
