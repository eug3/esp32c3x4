# VFS章节协议迁移说明

## 修改日期
2025-01-XX

## 背景
之前VFS使用page级别的BLE请求协议，现改为chapter级别，一次性下载完整章节文本。

## 协议变更

### 旧协议（已废弃）
```
PAGE:chapter:page
```
- ESP32向Client请求单个页面
- Client返回指定页的文本

### 新协议
```
PAGE:0x<book_hash>:<chapter_index>
```
- ESP32向Client请求完整章节
- Client返回整个章节的完整文本（通过X4IM v2协议）
- 章节存储为本地文件：`/littlefs/ble_vfs/0x<book_hash>_ch<chapter>.txt`

## 代码修改

### 1. vfs_ble_impl.c

#### 修改点1：prefetch_window_pages()
**修改前：**
```c
// 请求通过BLE获取page
request_page_from_client(cache, page);
```

**修改后：**
```c
// 只从本地文件加载（chapter已经通过BLE下载）
try_load_page_from_fs(cache, page, slot);
```

#### 修改点2：vfs_ble_read_pages()
**修改前：**
```c
if (!loaded_local) {
    request_page_from_client(cache, page);  // page级别请求
    ...
}
```

**修改后：**
```c
if (!loaded_local) {
    // 本地文件不存在，可能chapter还未下载
    ESP_LOGW(TAG, "Page %d not found in fs, chapter may not be downloaded yet", page);
    return false;
}
```

#### 修改点3：vfs_ble_switch_chapter()
**修改前：**
```c
// 切换章节后只预加载页面
prefetch_window_pages(cache);
```

**修改后：**
```c
// 先请求完整章节文本
request_chapter_from_client(cache);

// 再从本地文件预加载页面
prefetch_window_pages(cache);
```

### 2. request_chapter_from_client()函数

```c
static void request_chapter_from_client(ble_file_cache_t *cache) {
    char cmd[64];
    int len = snprintf(cmd, sizeof(cmd), "PAGE:0x%08lX:%d", 
                      cache->book_hash, cache->current_chapter);
    
    ESP_LOGI(TAG, "Requesting chapter: %s", cmd);
    ble_send_notification((uint8_t*)cmd, len);
}
```

## 工作流程

### 切换章节时
1. 用户选择新章节
2. ESP32调用`vfs_ble_switch_chapter(chapter_index)`
3. VFS调用`request_chapter_from_client()` → 发送`PAGE:0x12345678:5`
4. Client接收请求，从服务器获取chapter文本
5. Client通过X4IM v2协议分帧传输完整章节
6. ESP32收到所有帧后，拼接并保存到`/littlefs/ble_vfs/0x12345678_ch5.txt`
7. VFS从本地文件预加载前几页到缓存

### 翻页时
1. 用户按Volume+/-翻页
2. VFS检查page缓存
3. **未命中：** 从本地chapter文件加载（不再通过BLE请求）
4. **命中：** 直接返回缓存数据
5. 预加载下一页（从本地文件）

## 优势
1. **减少BLE流量**：每个chapter只传输一次（vs 每页传输）
2. **提高响应速度**：翻页直接读本地文件
3. **简化协议**：不需要page级别的同步
4. **离线阅读**：chapter下载后可断开BLE继续阅读

## 注意事项
1. **首次加载**：切换章节时需等待BLE传输完成
2. **存储空间**：需要足够的littlefs空间存储完整章节
3. **内存使用**：X4IM接收缓冲区需支持大章节（已实现动态扩容）
4. **错误处理**：若chapter文件不存在，需提示用户等待下载

## 相关文件
- `/Users/beijihu/Github/esp32c3x4/main/ui/vfs/vfs_ble_impl.c` - VFS实现
- `/Users/beijihu/Github/esp32c3x4/main/ui/screens/ble_reader_screen.c` - 章节浏览UI
- `/Users/beijihu/Github/BleReadBook/BleClient/BLE_CHAPTER_PROTOCOL.md` - 协议文档
- `/Users/beijihu/Github/BleReadBook/BleClient/src/chapter_protocol_handler.js` - Client处理器

## 测试要点
- [ ] 切换章节时正确发送`PAGE:book:chapter`
- [ ] Client正确接收并获取章节文本
- [ ] X4IM帧正确传输大章节（测试10KB+文本）
- [ ] ESP32正确保存chapter文件到littlefs
- [ ] 翻页从本地文件加载（不发送BLE请求）
- [ ] 内存占用正常（大章节不溢出）
