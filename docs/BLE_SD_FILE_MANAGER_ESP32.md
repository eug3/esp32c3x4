# ESP32 蓝牙 SD 卡文件管理系统实现

## 概述

本文档描述了 ESP32 端 BLE SD 卡文件管理系统的实现细节。

## 文件结构

```
main/ui/screens/
└── ble_reader_screen.c    # 主要实现文件
```

## 命令处理实现

### 命令定义 (ble_reader_screen.c)

```c
#define X4IM_CMD_FILE_NOTIFY 0x8B   // 文件通知
#define X4IM_CMD_SET_MODE   0x8C    // 设置工作模式
#define X4IM_CMD_GET_MODE   0x8D    // 查询当前模式
#define X4IM_CMD_LIST_FILES 0x8E    // 列表 SD 卡文件
#define X4IM_CMD_DELETE_FILE 0x8F   // 删除 SD 卡文件/目录
#define X4IM_CMD_RENAME_FILE 0x90   // 重命名 SD 卡文件/目录
#define X4IM_CMD_CLEAR_BOOKS 0x91   // 清空书籍目录
#define X4IM_CMD_CREATE_DIR 0x92    // 创建目录
#define X4IM_CMD_GET_STORAGE_INFO 0x93  // 获取存储信息
#define X4IM_CMD_READ_FILE 0x94     // 读取文件（下载）
#define X4IM_CMD_FILE_DATA 0x95     // 文件数据块
```

## 功能实现详解

### 1. 文件列表 (LIST_FILES)

**接收格式**: `[0x8E, path_len, path...]` 或 `[0x8E]`

**实现**:
```c
if (length >= 1 && data[0] == X4IM_CMD_LIST_FILES) {
    const char *scan_path = "/sdcard";
    
    // 解析路径参数（可选）
    if (length > 1) {
        int path_len = data[1];
        memcpy(path_buf, &data[2], path_len);
        scan_path = path_buf;
    }
    
    DIR *dir = opendir(scan_path);
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // 构建响应: [0x8B, flags, size(4), filename...]
        uint8_t response[300];
        response[0] = X4IM_CMD_FILE_NOTIFY;
        response[1] = entry->d_type == DT_DIR ? 0x01 : 0x00;
        
        // 获取文件大小
        struct stat st;
        stat(full_path, &st);
        uint32_t size = st.st_size;
        response[2] = (size >> 0) & 0xFF;
        response[3] = (size >> 8) & 0xFF;
        response[4] = (size >> 16) & 0xFF;
        response[5] = (size >> 24) & 0xFF;
        
        // 文件名
        memcpy(&response[6], entry->d_name, name_len);
        ble_manager_send_data(response, 6 + name_len);
    }
    
    closedir(dir);
    
    // 发送结束标记
    uint8_t end_marker[2] = {X4IM_CMD_FILE_NOTIFY, 0xFF};
    ble_manager_send_data(end_marker, 2);
}
```

### 2. 文件读取/下载 (READ_FILE)

**接收格式**: `[0x94, offset(4), size(4), filepath...]`

**实现流程**:
1. 解析请求参数
2. 打开文件
3. 定位到指定偏移量
4. 分块读取并发送（每块256字节）
5. 发送完成标记

**代码实现**:
```c
if (length > 9 && data[0] == X4IM_CMD_READ_FILE) {
    uint32_t offset = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    uint32_t read_size = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
    
    // 解析文件路径
    char filepath[490];
    int path_len = length - 9;
    memcpy(filepath, &data[9], path_len);
    filepath[path_len] = '\0';
    
    // 构建完整路径
    char full_path[512];
    if (filepath[0] == '/') {
        snprintf(full_path, sizeof(full_path), "/sdcard%s", filepath);
    } else {
        snprintf(full_path, sizeof(full_path), "/sdcard/%s", filepath);
    }
    
    // 打开文件
    FILE *file = fopen(full_path, "rb");
    if (!file) {
        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x04};  // not found
        ble_manager_send_data(response, 2);
        return;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // 调整读取大小
    if (offset + read_size > file_size) {
        read_size = file_size - offset;
    }
    
    // 分块发送
    const uint32_t CHUNK_SIZE = 256;
    uint8_t *chunk_buffer = malloc(CHUNK_SIZE + 13);
    fseek(file, offset, SEEK_SET);
    uint32_t sent = 0;
    
    while (sent < read_size) {
        uint32_t chunk_len = (read_size - sent > CHUNK_SIZE) ? CHUNK_SIZE : (read_size - sent);
        size_t bytes_read = fread(&chunk_buffer[13], 1, chunk_len, file);
        
        if (bytes_read == 0) break;
        
        // 构建数据包: [0x95, offset(4), total_size(4), chunk_size(4), data...]
        chunk_buffer[0] = X4IM_CMD_FILE_DATA;
        
        uint32_t current_offset = offset + sent;
        chunk_buffer[1] = (current_offset >> 0) & 0xFF;
        chunk_buffer[2] = (current_offset >> 8) & 0xFF;
        chunk_buffer[3] = (current_offset >> 16) & 0xFF;
        chunk_buffer[4] = (current_offset >> 24) & 0xFF;
        
        chunk_buffer[5] = (file_size >> 0) & 0xFF;
        chunk_buffer[6] = (file_size >> 8) & 0xFF;
        chunk_buffer[7] = (file_size >> 16) & 0xFF;
        chunk_buffer[8] = (file_size >> 24) & 0xFF;
        
        chunk_buffer[9] = (bytes_read >> 0) & 0xFF;
        chunk_buffer[10] = (bytes_read >> 8) & 0xFF;
        chunk_buffer[11] = (bytes_read >> 16) & 0xFF;
        chunk_buffer[12] = (bytes_read >> 24) & 0xFF;
        
        ble_manager_send_data(chunk_buffer, 13 + bytes_read);
        sent += bytes_read;
        
        // 延迟避免缓冲区溢出
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    free(chunk_buffer);
    fclose(file);
    
    // 发送完成标记
    uint8_t end_marker[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};
    ble_manager_send_data(end_marker, 2);
}
```

### 3. 文件删除 (DELETE_FILE)

**接收格式**: `[0x8F, filepath...]`

**实现**:
```c
if (length > 1 && data[0] == X4IM_CMD_DELETE_FILE) {
    char filepath[490];
    int name_len = length - 1;
    memcpy(filepath, &data[1], name_len);
    filepath[name_len] = '\0';
    
    // 构建完整路径
    char full_path[512];
    if (filepath[0] == '/') {
        snprintf(full_path, sizeof(full_path), "/sdcard%s", filepath);
    } else {
        snprintf(full_path, sizeof(full_path), "/sdcard/%s", filepath);
    }
    
    struct stat st;
    if (stat(full_path, &st) == 0) {
        int result;
        if (S_ISDIR(st.st_mode)) {
            // 目录：检查是否为空
            DIR *dir = opendir(full_path);
            int count = 0;
            struct dirent *e;
            while ((e = readdir(dir)) != NULL) {
                if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
                    count++;
                }
            }
            closedir(dir);
            
            if (count > 0) {
                uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x03};  // not empty
                ble_manager_send_data(response, 2);
            } else {
                result = rmdir(full_path);
                uint8_t status = (result == 0) ? 0x01 : 0x02;
                uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, status};
                ble_manager_send_data(response, 2);
            }
        } else {
            // 文件
            result = unlink(full_path);
            uint8_t status = (result == 0) ? 0x01 : 0x02;
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, status};
            ble_manager_send_data(response, 2);
        }
    } else {
        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x04};  // not found
        ble_manager_send_data(response, 2);
    }
}
```

### 4. 文件重命名 (RENAME_FILE)

**接收格式**: `[0x90, old_len, oldname..., new_len, newname...]`

**实现**:
```c
if (length > 2 && data[0] == X4IM_CMD_RENAME_FILE) {
    int offset = 1;
    int old_len = data[offset++];
    
    char oldpath[490];
    memcpy(oldpath, &data[offset], old_len);
    oldpath[old_len] = '\0';
    offset += old_len;
    
    int new_len = data[offset++];
    char newpath[490];
    memcpy(newpath, &data[offset], new_len);
    newpath[new_len] = '\0';
    
    // 构建完整路径
    char full_oldpath[512], full_newpath[512];
    snprintf(full_oldpath, sizeof(full_oldpath), "/sdcard/%s", oldpath);
    snprintf(full_newpath, sizeof(full_newpath), "/sdcard/%s", newpath);
    
    int result = rename(full_oldpath, full_newpath);
    uint8_t status = (result == 0) ? 0x01 : 0x02;
    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, status};
    ble_manager_send_data(response, 2);
}
```

### 5. 创建目录 (CREATE_DIR)

**接收格式**: `[0x92, dirname...]`

**实现**:
```c
if (length > 1 && data[0] == X4IM_CMD_CREATE_DIR) {
    char dirpath[512];
    int name_len = length - 1;
    memcpy(dirpath, &data[1], name_len);
    dirpath[name_len] = '\0';
    
    // 确保以 /sdcard/ 开头
    if (dirpath[0] != '/') {
        memmove(dirpath + 8, dirpath, name_len + 1);
        memcpy(dirpath, "/sdcard/", 8);
    }
    
    // mkdir 成功或目录已存在都返回成功
    if (mkdir(dirpath, 0755) == 0 || errno == EEXIST) {
        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};
        ble_manager_send_data(response, 2);
    } else {
        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};
        ble_manager_send_data(response, 2);
    }
}
```

### 6. 存储信息 (GET_STORAGE_INFO)

**接收格式**: `[0x93]`

**响应格式**: `[0x93, total(4), used(4), files(2), dirs(2)]`

**实现**:
```c
if (length == 1 && data[0] == X4IM_CMD_GET_STORAGE_INFO) {
    uint64_t total_size = 0;
    uint64_t used_size = 0;
    int file_count = 0;
    int dir_count = 0;
    
    // 使用 FATFS 获取实际容量
    FATFS *fs;
    DWORD fre_clust, fre_bsec, tot_bsec;
    if (f_getfree("/sdcard", &fre_clust, &fs) == 0) {
        tot_bsec = fs->csize * fs->n_fatent;
        fre_bsec = fre_clust * fs->csize;
        total_size = (uint64_t)tot_bsec * 512;
        uint64_t free_size = (uint64_t)fre_bsec * 512;
        used_size = total_size - free_size;
    }
    
    // 递归统计文件和目录数量
    scan_directory_recursive("/sdcard", &file_count, &dir_count, &used_size);
    
    // 构建响应
    uint8_t response[13];
    response[0] = X4IM_CMD_GET_STORAGE_INFO;
    
    uint32_t total32 = (total_size > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)total_size;
    response[1] = (total32 >> 0) & 0xFF;
    response[2] = (total32 >> 8) & 0xFF;
    response[3] = (total32 >> 16) & 0xFF;
    response[4] = (total32 >> 24) & 0xFF;
    
    uint32_t used32 = (used_size > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)used_size;
    response[5] = (used32 >> 0) & 0xFF;
    response[6] = (used32 >> 8) & 0xFF;
    response[7] = (used32 >> 16) & 0xFF;
    response[8] = (used32 >> 24) & 0xFF;
    
    response[9] = (file_count >> 0) & 0xFF;
    response[10] = (file_count >> 8) & 0xFF;
    response[11] = (dir_count >> 0) & 0xFF;
    response[12] = (dir_count >> 8) & 0xFF;
    
    ble_manager_send_data(response, 13);
}
```

### 辅助函数

#### scan_directory_recursive

递归扫描目录统计文件和目录数量：

```c
static void scan_directory_recursive(const char *path, int *file_count, int *dir_count, uint64_t *used_size)
{
    DIR *dir = opendir(path);
    if (!dir) return;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                (*dir_count)++;
                scan_directory_recursive(full_path, file_count, dir_count, used_size);
            } else {
                (*file_count)++;
                *used_size += st.st_size;
            }
        }
    }
    
    closedir(dir);
}
```

#### build_books_path

构建 /sdcard/books/ 路径：

```c
static void build_books_path(char *output, size_t output_size, const char *filename)
{
    snprintf(output, output_size, "/sdcard/books/%s", filename);
}
```

## 工作模式管理

### 模式定义
```c
typedef enum {
    BLE_MODE_READING = 0,   // 阅读模式
    BLE_MODE_TRANSFER = 1   // 传输模式
} ble_work_mode_t;
```

### 模式切换
```c
void ble_reader_set_mode(ble_work_mode_t mode)
{
    s_ble_state.work_mode = mode;
    
    if (mode == BLE_MODE_READING) {
        // 阅读模式：清除传输状态
        s_ble_state.transfer_bytes_received = 0;
        s_ble_state.transfer_bytes_total = 0;
        s_ble_state.transfer_file_count = 0;
    } else {
        // 传输模式：准备接收文件
        mkdir("/sdcard/books", 0755);
    }
}
```

## 文件上传处理 (X4IM v2)

### 帧头解析
```c
if (data[0] == 'X' && data[1] == '4' && data[2] == 'I' && data[3] == 'M') {
    // 解析标志
    uint16_t flags = data[6] | (data[7] << 8);
    uint32_t payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    
    // 提取文件名（v2，偏移16）
    char recv_filename[16] = {0};
    memcpy(recv_filename, &data[16], 15);
    
    // 检查 SD 卡存储标志
    bool use_sd = (flags & X4IM_FLAGS_STORAGE_SD) != 0;
    
    // 模式检查
    if (use_sd && s_ble_state.work_mode == BLE_MODE_READING) {
        ESP_LOGW(TAG, "SD write rejected in READING mode");
        return;
    }
    
    if (s_ble_state.work_mode == BLE_MODE_TRANSFER && !use_sd) {
        use_sd = true;  // 传输模式自动升级为 SD 存储
    }
    
    // 打开文件进行流式写入
    if (use_sd) {
        mkdir("/sdcard/books", 0755);
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/sdcard/books/%s", recv_filename);
        x4im_rx_state.file_handle = fopen(filepath, "wb");
    }
}
```

### 数据写入
```c
// 数据部分：直接写入文件
if (x4im_rx_state.file_handle != NULL) {
    size_t written = fwrite(payload, 1, payload_len, x4im_rx_state.file_handle);
    x4im_rx_state.received_bytes += written;
    
    if (x4im_rx_state.received_bytes >= x4im_rx_state.expected_size) {
        fclose(x4im_rx_state.file_handle);
        x4im_rx_state.file_handle = NULL;
        ESP_LOGI(TAG, "File transfer complete: %s", x4im_rx_state.filename);
    }
}
```

## 性能优化

### 1. 流式处理
- 上传：边接收边写入，无需完整缓存
- 下载：边读取边发送，内存占用小

### 2. 分块传输
- 下载块大小：256字节
- 块间延迟：20ms（避免蓝牙缓冲区溢出）

### 3. 路径缓存
- 避免重复构建路径字符串
- 使用栈上缓冲区减少堆分配

## 调试日志

启用详细日志：
```c
static const char *TAG = "BLE_READER";

ESP_LOGI(TAG, "List files in: %s", scan_path);
ESP_LOGI(TAG, "Deleting: %s", full_path);
ESP_LOGI(TAG, "Renaming: %s -> %s", old_path, new_path);
ESP_LOGI(TAG, "Reading file: %s (offset=%u, size=%u)", filepath, offset, read_size);
ESP_LOGI(TAG, "Sent chunk: offset=%u, size=%zu", current_offset, bytes_read);
```

## 错误处理

### 常见错误
```c
// 文件不存在
if (!file) {
    ESP_LOGW(TAG, "File not found: %s", filepath);
    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x04};
    ble_manager_send_data(response, 2);
}

// 目录不为空
if (dir_count > 0) {
    ESP_LOGW(TAG, "Directory not empty: %s", dirpath);
    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x03};
    ble_manager_send_data(response, 2);
}

// 操作失败
if (result != 0) {
    ESP_LOGW(TAG, "Operation failed (errno=%d)", errno);
    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};
    ble_manager_send_data(response, 2);
}
```

## 安全考虑

### 路径验证
```c
// 防止路径遍历攻击
if (strstr(filepath, "..") != NULL) {
    ESP_LOGW(TAG, "Invalid path containing '..'");
    return;
}

// 确保路径在 /sdcard/ 下
if (strncmp(full_path, "/sdcard/", 8) != 0) {
    ESP_LOGW(TAG, "Path outside /sdcard/");
    return;
}
```

### 缓冲区溢出保护
```c
// 限制路径长度
if (path_len > 490) path_len = 490;

// 限制文件名长度
if (name_len > 255) name_len = 255;
```

## 编译配置

在 `sdkconfig` 中启用必要的组件：
```
CONFIG_FATFS_LONG_FILENAMES=y
CONFIG_FATFS_LFN_HEAP=y
CONFIG_BT_ENABLED=y
CONFIG_BLUEDROID_ENABLED=y
```

## 测试建议

### 单元测试
1. 测试各命令的正确响应
2. 测试边界条件（空目录、大文件、长文件名）
3. 测试错误处理（文件不存在、权限不足）

### 集成测试
1. 完整的上传/下载流程
2. 多文件批量操作
3. 长时间运行稳定性测试

### 性能测试
1. 大文件传输速度
2. 内存使用情况
3. 蓝牙传输稳定性

## 已知限制

1. **路径长度**: 最大 490 字节
2. **文件名长度**: 最大 255 字节
3. **传输速度**: 约 10-20 KB/s（受蓝牙带宽限制）
4. **并发限制**: 同时只能处理一个文件传输

## 未来改进

1. **压缩传输**: 支持文件压缩以提高传输速度
2. **断点续传**: 大文件传输中断后可继续
3. **批量操作**: 一次命令处理多个文件
4. **异步处理**: 使用任务队列处理长时间操作

## 参考资料

- [ESP-IDF FATFS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/fatfs.html)
- [ESP-IDF BLE 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/index.html)
- POSIX 文件系统 API

## 版本历史

- **v2.0** (2026-01-14)
  - ✅ 添加文件下载功能
  - ✅ 完善存储统计
  - ✅ 优化传输性能

- **v1.0** (初始版本)
  - ✅ 基础文件管理命令

## 许可证

MIT License
