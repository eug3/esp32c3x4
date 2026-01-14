# BLE 图像接收流式写入优化

## 问题描述
在通过蓝牙接收 X4IM 格式的位图数据时（48000 字节），尝试一次性分配大内存缓冲区导致内存分配失败：
```
E (6810809) BLE_READER: Failed to allocate 48000 bytes for bitmap
```

ESP32C3 的可用堆内存有限，无法一次性分配如此大的连续内存块。

## 解决方案
采用 **流式写入** (streaming write) 到 littlefs 文件系统：

### 修改前
```c
// 尝试分配 48KB 内存缓冲区
x4im_rx_state.buffer = (uint8_t *)malloc(payload_size);
```

### 修改后
```c
// 直接打开文件准备流式写入
x4im_rx_state.file_handle = fopen(filename, "wb");
```

## 实现细节

### 1. 数据结构修改
```c
// 旧版本
static struct {
    uint8_t *buffer;  // 大内存缓冲区
    ...
} x4im_rx_state;

// 新版本
static struct {
    FILE *file_handle;  // 文件句柄，流式写入
    char filename[64];  // 当前写入的文件名
    ...
} x4im_rx_state;
```

### 2. 接收流程

#### 接收帧头
1. 解析 X4IM 帧头，获取 payload_size
2. 生成目标文件路径
3. 以写入模式打开文件
4. 如果帧头包含数据，直接写入文件

#### 继续接收数据块
1. 每次接收到数据包，直接调用 `fwrite()` 写入文件
2. 更新已接收字节计数
3. 无需维护大内存缓冲区

#### 完成接收
1. 检测到数据接收完成
2. 关闭文件
3. 触发页面刷新显示

### 3. 清理机制
- 在屏幕隐藏时（`on_hide`）
- 在接收新帧头时（如果有未完成传输）
- 在 `deinit_x4im_mutex` 中确保关闭文件

## 优势
✅ **零大内存分配**：无需分配 48KB 连续内存  
✅ **实时写入**：数据到达即写入文件，不积累在内存  
✅ **稳定可靠**：避免内存碎片和分配失败  
✅ **适应性强**：可处理任意大小的位图数据  

## 内存使用对比
| 方案 | 内存占用 |
|------|---------|
| 旧方案（内存缓冲） | 48000 字节 |
| 新方案（流式写入） | ~512 字节（文件系统缓冲） |

## 测试验证
编译通过，等待设备测试：
```bash
idf.py build flash monitor
```

## 相关文件
- [ble_reader_screen.c](../main/ui/screens/ble_reader_screen.c)
