# XTEINK X4 自定义应用部署流程

## 准备工作完成 ✓

- [x] app1 原厂固件备份 → `app1_backup.bin`
- [x] 测试应用代码 → `test_app_xteink/test_app_xteink.ino`
- [x] OTA 切换脚本 → `otadata_select_ota_0.bin`, `otadata_select_ota_1.bin`

---

## 步骤 1: 在 Arduino IDE 中编译测试应用

### 1.1 打开项目
```
文件 → 打开 → d:\esp32c3x4\test_app_xteink\test_app_xteink.ino
```

### 1.2 配置开发板设置
```
工具菜单设置：
├─ 开发板: "ESP32C3 Dev Module"
├─ Flash Size: "16MB (128Mb)"
├─ Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
├─ Upload Speed: "921600"
├─ CPU Frequency: "160MHz"
└─ Port: "COM5"
```

### 1.3 编译（不上传）
```
点击: 项目 → 验证/编译
或快捷键: Ctrl+R
```

### 1.4 找到编译输出的 .bin 文件
编译后，Arduino 会显示输出路径，类似：
```
C:\Users\<用户名>\AppData\Local\Temp\arduino\sketches\<随机ID>\test_app_xteink.ino.bin
```

**复制这个完整路径！**

---

## 步骤 2: 手动烧录到 app1 分区

### 2.1 烧录测试应用到 0x650000
```powershell
# 替换 <编译输出路径> 为实际路径
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0x650000 "<编译输出路径>\test_app_xteink.ino.bin"
```

示例：
```powershell
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0x650000 "C:\Users\YourName\AppData\Local\Temp\arduino\sketches\sketch_ABC123\test_app_xteink.ino.bin"
```

### 2.2 设置启动到 app1
```powershell
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_select_ota_1.bin
```

### 2.3 重启设备
```powershell
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 run
```

---

## 步骤 3: 查看串口输出

```powershell
d:\esp32c3x4\esp32_env\Scripts\python.exe -m serial.tools.miniterm COM5 115200
```

**预期输出：**
```
========================================
  XTEINK X4 自定义应用
========================================

硬件信息:
  型号: esp32c3
  核心数: 1
  Flash: 16 MB external
  WiFi: YES
  BLE: YES

分区信息:
  运行分区: app1
  地址: 0x00650000
  大小: 6400 KB
  启动分区: app1

检测到在 app1 运行，设置为默认启动分区...
✓ 已设置 app1 为默认启动分区

========================================
  应用启动成功！
========================================

开始心跳输出...
[1] 心跳 - 运行时间: 1 秒
[2] 心跳 - 运行时间: 2 秒
...
```

---

## 步骤 4: 切换回原厂固件（如需要）

### 方法 1: 只切换启动分区（推荐）
```powershell
# 切换到 app0 (原厂固件仍在 0x010000)
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_select_ota_0.bin

# 重启
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 run
```

### 方法 2: 恢复 app1 分区
```powershell
# 恢复 app1 为原厂固件
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0x650000 d:\esp32c3x4\app1_backup.bin
```

### 方法 3: 完整恢复
```powershell
# 恢复整个 flash
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0x0 d:\esp32c3x4\flash_dump.bin
```

---

## 故障排查

### 问题 1: 编译失败
**错误:** `esp_ota_ops.h: No such file or directory`

**解决:** 
- 确认选择的是 ESP32C3 开发板（不是 ESP32）
- 更新 ESP32 开发板支持包：工具 → 开发板 → 开发板管理器 → ESP32 → 更新

### 问题 2: 设备启动失败
**症状:** 重启后无输出或循环重启

**解决:**
```powershell
# 1. 查看启动日志
d:\esp32c3x4\esp32_env\Scripts\python.exe -m serial.tools.miniterm COM5 115200

# 2. 如果看到分区错误，恢复原厂固件
d:\esp32c3x4\esp32_env\Scripts\python.exe -m esptool -p COM5 --chip esp32c3 write_flash 0xE000 d:\esp32c3x4\otadata_select_ota_0.bin
```

### 问题 3: 找不到编译输出文件
**解决:**
1. 编译时查看 Arduino IDE 底部输出窗口
2. 最后几行会显示 `.bin` 文件路径
3. 或者在 Arduino 设置中启用"编译时显示详细输出"

---

## 下一步开发

测试应用成功后，你可以：

1. **添加墨水屏支持**
   - 分析原厂固件的 GPIO 配置
   - 使用 GxEPD2 库或其他 e-ink 库
   
2. **添加 WiFi 功能**
   - 连接网络
   - OTA 远程更新
   
3. **添加按键交互**
   - 检测设备按键
   - 实现自定义功能

4. **文件系统**
   - 使用 SPIFFS (0xC90000, 3.4MB)
   - 存储配置/数据

---

## 重要提醒

⚠️ **安全提示：**
- app0 保持原厂固件，作为安全回退
- 每次修改前确保有完整备份
- 不要覆盖 bootloader (0x0) 和分区表 (0x8000)
- 测试新功能前先在 app1 验证

✅ **成功标志：**
- 串口看到"自定义应用已启动"
- 运行分区显示为 app1 (0x00650000)
- 心跳正常输出
