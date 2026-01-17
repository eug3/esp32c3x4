# BLE 双重释放崩溃修复

## 问题描述

ESP32 设备在接收 BLE 数据时崩溃，错误信息：
```
assert failed: tlsf_free tlsf.c:630 (!block_is_free(block) && "block already marked as free")
```

这是一个典型的**双重释放（double free）**错误，同一块内存被释放了多次。

## 崩溃日志

```
I (139411) BLE_READER: X4IM v2 header: payload_size=4501
I (139421) BLE_READER: X4IM frame: v2, flags=0x0004, payload=4501, sd=0, name='weread_0'
I (139441) BLE_READER: Opened file for streaming: /littlefs/ble_slots/0x00000000_slot0.txt (SD=0)
I (139441) BLE_READER: Wrote 480 bytes from header packet (480/4501)

assert failed: tlsf_free tlsf.c:630 (!block_is_free(block) && "block already marked as free")
```

## 根本原因分析

在 `ble_reader_screen.c` 的 `handle_page_data()` 函数中，`data` 指针被**多次释放**：

### 问题代码路径

1. **第 1367 行** - Header packet 错误处理：
   ```c
   fclose(x4im_rx_state.file_handle);
   x4im_rx_state.file_handle = NULL;
   x4im_rx_state.receiving = false;
   xSemaphoreGive(x4im_rx_mutex);
   free((void *)data);  // ❌ 第一次释放
   return;
   ```

2. **第 1433 行** - Header packet 处理完成：
   ```c
   }
   
   // 处理完毕，释放内存
   free((void *)data);  // ❌ 第二次释放（正常路径也会走到这里）
   return;
   ```

3. **第 1452 行** - 继续接收数据时的错误处理：
   ```c
   ESP_LOGE(TAG, "File write error");
   fclose(x4im_rx_state.file_handle);
   free((void *)data);  // ❌ 释放
   x4im_rx_state.file_handle = NULL;
   ```

4. **第 1589 行** - 所有路径的统一释放：
   ```c
   // 所有路径最后都要释放内存
   free((void *)data);  // ❌ 又一次释放
   ```

### 问题场景

当处理 header packet 时：
- BleClient 发送第一个包：**header(32字节) + 首块数据(480字节)** = 512字节
- ESP32 处理完 header 后，在第 1433 行 `free(data)`
- 但由于代码结构，**继续往下执行到第 1589 行又 `free(data)` 一次**
- 导致双重释放崩溃

## 修复方案

### 原则

**每个 `data` 指针在所有代码路径中只能被释放一次**。

### 修复策略

在每个处理分支完成后**立即释放内存并 `return`**，避免执行到统一释放点：

```c
// ✅ Header packet 处理
if (complete) {
    // ... 处理逻辑 ...
    free((void *)data);
    return;  // 立即返回
} else {
    xSemaphoreGive(x4im_rx_mutex);
    free((void *)data);
    return;  // 立即返回
}

// ✅ 继续接收数据
if (write_error) {
    fclose(...);
    xSemaphoreGive(x4im_rx_mutex);
    free((void *)data);
    return;  // 立即返回
}

if (complete) {
    // ... 处理完成 ...
    free((void *)data);
    return;
} else {
    xSemaphoreGive(x4im_rx_mutex);
    free((void *)data);
    return;
}

// ✅ 其他分支
if (!receiving) {
    xSemaphoreGive(x4im_rx_mutex);
    free((void *)data);
    return;
}

// ✅ 最后的兜底
free((void *)data);
```

### 修复后的代码流程

每个代码分支都明确：
1. 完成自己的处理逻辑
2. 释放 `data` 内存
3. 立即 `return`

**不会**再有"统一释放点"导致的重复释放。

## X4IM v2 协议规范（已对齐）

### BleClient 发送端

**Header 构造**（32 字节）：
```javascript
const header = new Uint8Array(32);
header[0] = 0x58;  // 'X'
header[1] = 0x34;  // '4'
header[2] = 0x49;  // 'I'
header[3] = 0x4D;  // 'M'
header[4] = 0x02;  // Version = 2
header[5] = type;  // 文件类型
header[6] = flags & 0xFF;        // Flags 低字节
header[7] = (flags >> 8) & 0xFF; // Flags 高字节
header[8] = payloadSize & 0xFF;         // Payload 大小（小端序）
header[9] = (payloadSize >> 8) & 0xFF;
header[10] = (payloadSize >> 16) & 0xFF;
header[11] = (payloadSize >> 24) & 0xFF;
header[12] = sequence & 0xFF;           // 序列号（小端序）
header[13] = (sequence >> 8) & 0xFF;
header[14] = 0x00;  // Reserved
header[15] = 0x00;
// header[16..31] = filename (UTF-8, null-terminated)
```

**发送流程**：
1. 第一个包 = `header(32) + data.slice(0, MTU-32)`
   - MTU=512 时，首包 = 32 + 480 = 512 字节
2. 后续包：每次发送 `data.slice(offset, offset + MTU)`
   - MTU=512 时，每包最多 512 字节

### ESP32 接收端

**解析 Header**：
```c
uint16_t flags = data[6] | (data[7] << 8);
uint32_t payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
char recv_filename[16] = {0};
memcpy(recv_filename, &data[16], 15);
recv_filename[15] = '\0';
bool use_sd = (flags & X4IM_FLAGS_STORAGE_SD) != 0;
```

**接收流程**：
1. **收到 header packet**：
   - 解析 header
   - 打开文件
   - 写入 `data[32..length-1]` 到文件（如 480 字节）
   - 检查是否完成（单包传输）
   - 释放内存并返回

2. **继续接收数据包**：
   - 直接写入整个包到文件
   - 更新已接收字节数
   - 检查是否完成
   - 释放内存并返回

## 验证清单

修复后应该看到：

✅ **成功日志**：
```
I (xxx) BLE_READER: X4IM v2 header: payload_size=4501
I (xxx) BLE_READER: Opened file for streaming: /littlefs/ble_slots/xxx.txt
I (xxx) BLE_READER: Wrote 480 bytes from header packet (480/4501)
I (xxx) BLE_READER: Streaming to file: 970/4501 bytes (21.5%)
I (xxx) BLE_READER: Streaming to file: 1450/4501 bytes (32.2%)
...
I (xxx) BLE_READER: ======== FILE RECEPTION COMPLETE ========
I (xxx) BLE_READER: Size: 4501 bytes
```

❌ **不应该再出现**：
```
assert failed: tlsf_free tlsf.c:630
```

## 总结

- **问题**：多路径重复释放同一块内存
- **修复**：每个分支处理完后立即释放并返回
- **协议**：BleClient 和 ESP32 端 X4IM v2 协议完全对齐
- **状态**：已修复，等待测试验证

---
**修复时间**: 2026-01-16  
**影响范围**: esp32c3x4/main/ui/screens/ble_reader_screen.c  
**协议版本**: X4IM v2
