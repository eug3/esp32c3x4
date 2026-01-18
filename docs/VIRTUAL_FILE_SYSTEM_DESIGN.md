# 虚拟文件系统设计 - BLE远程文本无缝阅读方案

## 问题分析

### 当前问题
1. **BLE传输不稳定**: 网络延迟、分包丢失导致阅读卡顿
2. **双重逻辑**: 本地文件读取 vs BLE实时传输,代码冗余
3. **用户体验差**: BLE阅读需要等待加载,无法像本地书籍一样流畅

### 解决目标
**让ESP32读取BLE远程txt就像读取本地文件一样流畅**

---

## 🎯 核心方案:虚拟文件系统 (VFS)

### 架构设计

```
┌─────────────────────────────────────────────────┐
│           阅读器界面层 (reader_screen.c)         │
│         使用统一的 txt_reader API               │
└──────────────────┬──────────────────────────────┘
                   │
                   │ 统一接口
                   ▼
┌─────────────────────────────────────────────────┐
│        虚拟文件系统抽象层 (vfs_reader.h)         │
│  - vfs_open()                                   │
│  - vfs_read()                                   │
│  - vfs_seek()                                   │
│  - vfs_close()                                  │
└──────────────┬─────────────────┬────────────────┘
               │                 │
      ┌────────▼──────┐    ┌─────▼──────────┐
      │ 本地文件实现   │    │  BLE虚拟文件    │
      │ (SD卡/LittleFS)│    │  (远程缓存)     │
      └───────────────┘    └────────────────┘
```

---

## 📋 实现细节

### 1. 虚拟文件对象

```c
// vfs_reader.h
typedef enum {
    VFS_SOURCE_LOCAL,    // 本地文件 (SD/LittleFS)
    VFS_SOURCE_BLE       // BLE远程文件
} vfs_source_type_t;

typedef struct {
    vfs_source_type_t source;
    char identifier[256];       // 本地:文件路径, BLE:书籍URL/ID
    
    // 统一接口
    void *handle;              // 实际文件句柄或缓存管理器
    long position;             // 当前读取位置
    long total_size;           // 总大小(本地确定,BLE估算)
    
    // BLE特有
    struct {
        uint32_t book_hash;
        uint32_t chapter_hash;
        bool prefetch_enabled;
        int cache_window_size;  // 预缓存页数
    } ble_ctx;
} vfs_file_t;
```

### 2. 统一读取API

```c
// vfs_reader.h

/**
 * @brief 打开虚拟文件 (自动检测本地/BLE)
 */
vfs_file_t* vfs_open(const char *identifier);

/**
 * @brief 读取数据 (自动从本地或BLE缓存)
 */
int vfs_read(vfs_file_t *file, void *buffer, size_t size);

/**
 * @brief 定位到指定位置
 * - 本地:直接seek
 * - BLE:触发预加载对应页面
 */
bool vfs_seek(vfs_file_t *file, long offset, int whence);

/**
 * @brief 关闭文件
 */
void vfs_close(vfs_file_t *file);

/**
 * @brief 获取当前位置
 */
long vfs_tell(vfs_file_t *file);

/**
 * @brief 获取文件大小
 */
long vfs_size(vfs_file_t *file);
```

---

## 🔧 BLE虚拟文件实现

### 核心机制:智能预缓存 + 滑动窗口

```c
// vfs_ble_impl.c

typedef struct {
    // 缓存窗口 (例如:当前页 ± 3页)
    int current_page;
    int window_size;        // 5-10页
    
    // 页面缓存 (key: page_index, value: txt_content)
    struct {
        int page_index;
        char *content;      // 动态分配
        size_t content_len;
        bool ready;
    } page_cache[10];
    
    // 请求队列
    int pending_requests[10];
    int pending_count;
    
    // 统计
    uint32_t cache_hits;
    uint32_t cache_misses;
} ble_file_cache_t;

/**
 * @brief BLE文件读取实现
 */
int vfs_ble_read(vfs_file_t *file, void *buffer, size_t size) {
    ble_file_cache_t *cache = (ble_file_cache_t*)file->handle;
    
    // 1. 计算需要的页面索引
    int start_page = file->position / BLE_PAGE_SIZE;
    int end_page = (file->position + size) / BLE_PAGE_SIZE;
    
    int bytes_read = 0;
    
    for (int page = start_page; page <= end_page; page++) {
        // 2. 检查缓存
        if (is_page_cached(cache, page)) {
            // 命中:直接从缓存读取
            int offset = (page == start_page) ? (file->position % BLE_PAGE_SIZE) : 0;
            int available = get_cached_page_size(cache, page) - offset;
            int to_read = (size - bytes_read < available) ? (size - bytes_read) : available;
            
            memcpy(buffer + bytes_read, get_cached_page(cache, page) + offset, to_read);
            bytes_read += to_read;
            file->position += to_read;
            
            cache->cache_hits++;
        } else {
            // 未命中:触发预加载
            ESP_LOGW(TAG, "Cache miss: page %d, triggering prefetch", page);
            request_page_from_bleclient(file, page);
            
            // 阻塞等待(可配置超时)
            if (!wait_for_page(cache, page, 5000)) {
                ESP_LOGE(TAG, "Page %d load timeout", page);
                return bytes_read; // 返回已读取的部分
            }
            
            cache->cache_misses++;
            // 重试读取
            page--;
        }
    }
    
    // 3. 后台预加载窗口
    prefetch_window_pages(cache, file->position);
    
    return bytes_read;
}

/**
 * @brief 预加载窗口页面
 */
void prefetch_window_pages(ble_file_cache_t *cache, long current_pos) {
    int current_page = current_pos / BLE_PAGE_SIZE;
    
    // 预加载 [current+1, current+window_size]
    for (int i = 1; i <= cache->window_size; i++) {
        int page = current_page + i;
        if (!is_page_cached(cache, page) && !is_pending(cache, page)) {
            request_page_async(cache, page);
        }
    }
    
    // 清理旧页面 [current-window_size-2, ...]
    cleanup_old_pages(cache, current_page - cache->window_size - 2);
}
```

---

## 🌐 BleClient端配合

### 优化:本地txt分页管理

```javascript
// BleClient/src/txt_manager.js

class TxtPageManager {
  constructor(txtContent) {
    this.fullText = txtContent;
    this.pageSize = 1024;  // 每页1KB
    this.totalPages = Math.ceil(txtContent.length / this.pageSize);
    this.pageCache = new Map();
  }
  
  /**
   * 获取指定页内容
   */
  getPage(pageIndex) {
    if (this.pageCache.has(pageIndex)) {
      return this.pageCache.get(pageIndex);
    }
    
    const start = pageIndex * this.pageSize;
    const end = Math.min(start + this.pageSize, this.fullText.length);
    const pageContent = this.fullText.substring(start, end);
    
    this.pageCache.set(pageIndex, pageContent);
    return pageContent;
  }
  
  /**
   * 预加载窗口页面
   */
  async prefetchWindow(centerPage, windowSize = 5) {
    const pages = [];
    for (let i = centerPage - windowSize; i <= centerPage + windowSize; i++) {
      if (i >= 0 && i < this.totalPages) {
        pages.push(this.getPage(i));
      }
    }
    return pages;
  }
  
  /**
   * 响应ESP32请求
   */
  async handlePageRequest(pageIndex) {
    const content = this.getPage(pageIndex);
    
    // 发送到ESP32
    await sendPageToDevice({
      filename: `page_${pageIndex}`,
      content: content,
      flags: X4IM_FLAGS.TYPE_TXT | X4IM_FLAGS.STORAGE_LITTLEFS
    });
    
    // 自动预加载相邻页面
    this.prefetchWindow(pageIndex, 3);
  }
}
```

### 主动推送 vs 被动拉取

```javascript
// 监听ESP32的页面请求
notifyCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
  const value = new TextDecoder().decode(event.target.value);
  
  if (value.startsWith('PAGE:')) {
    const pageIndex = parseInt(value.substring(5));
    txtManager.handlePageRequest(pageIndex);
  }
});

// 初始化:主动推送前3页
async function initializeReading(txtContent) {
  const manager = new TxtPageManager(txtContent);
  
  // 推送初始窗口 [0, 1, 2]
  for (let i = 0; i <= 2; i++) {
    await manager.handlePageRequest(i);
    await sleep(50); // 避免拥塞
  }
}
```

---

## 📊 性能优化

### 1. 智能窗口大小

```c
// 根据可用内存动态调整
int calculate_optimal_window_size(void) {
    size_t free_heap = esp_get_free_heap_size();
    
    if (free_heap > 200 * 1024) return 10;      // 200KB+ → 10页窗口
    else if (free_heap > 100 * 1024) return 5;  // 100KB+ → 5页窗口
    else return 3;                              // 低内存 → 3页窗口
}
```

### 2. LRU缓存淘汰

```c
void cleanup_old_pages(ble_file_cache_t *cache, int threshold_page) {
    for (int i = 0; i < 10; i++) {
        if (cache->page_cache[i].ready && 
            cache->page_cache[i].page_index < threshold_page) {
            
            // 释放旧页面
            free(cache->page_cache[i].content);
            cache->page_cache[i].content = NULL;
            cache->page_cache[i].ready = false;
            
            ESP_LOGI(TAG, "Evicted page %d", cache->page_cache[i].page_index);
        }
    }
}
```

### 3. 压缩传输(可选)

```javascript
// BleClient压缩文本
import pako from 'pako';

async function sendCompressedPage(pageIndex, textContent) {
  const compressed = pako.deflate(new TextEncoder().encode(textContent));
  
  await sendPageToDevice({
    filename: `page_${pageIndex}`,
    content: compressed,
    flags: X4IM_FLAGS.TYPE_TXT | X4IM_FLAGS.COMPRESS
  });
}
```

```c
// ESP32解压
#include "miniz.h"

bool decompress_page(const uint8_t *compressed, size_t comp_len, 
                     uint8_t *output, size_t *out_len) {
    return mz_uncompress(output, out_len, compressed, comp_len) == MZ_OK;
}
```

---

## 🎨 用户体验改进

### 无缝切换

```c
// 使用相同的阅读器代码
void open_book(const char *identifier) {
    vfs_file_t *file = vfs_open(identifier);
    
    if (file->source == VFS_SOURCE_LOCAL) {
        ESP_LOGI(TAG, "Reading local book: %s", identifier);
    } else {
        ESP_LOGI(TAG, "Reading BLE book: %s", identifier);
        // 完全相同的阅读体验!
    }
    
    // 统一的翻页逻辑
    char buffer[2048];
    vfs_read(file, buffer, sizeof(buffer));
    display_page(buffer);
}
```

### 加载指示

```c
// vfs_seek时显示预加载进度
bool vfs_seek(vfs_file_t *file, long offset, int whence) {
    if (file->source == VFS_SOURCE_BLE) {
        int target_page = offset / BLE_PAGE_SIZE;
        
        if (!is_page_cached(file->handle, target_page)) {
            display_message("预加载中...");
            request_page_sync(file, target_page);
        }
    }
    
    file->position = offset;
    return true;
}
```

---

## 📁 文件结构

```
esp32c3x4/main/ui/
├── vfs/
│   ├── vfs_reader.h          # 虚拟文件系统接口
│   ├── vfs_reader.c          # 核心实现
│   ├── vfs_local_impl.c      # 本地文件实现
│   └── vfs_ble_impl.c        # BLE虚拟文件实现
├── txt/
│   └── txt_reader.c          # 修改为使用vfs接口
└── screens/
    └── reader_screen.c       # 统一使用vfs

BleClient/src/
├── txt_manager.js            # TXT分页管理
├── ble_page_protocol.js      # 页面请求协议
└── main.js                   # 集成
```

---

## 🚀 迁移步骤

### Phase 1: 创建VFS层
1. ✅ 实现 `vfs_reader.h` 接口定义
2. ✅ 实现 `vfs_local_impl.c` (包装现有文件操作)
3. ✅ 实现 `vfs_ble_impl.c` (缓存+请求机制)

### Phase 2: 修改txt_reader
1. ✅ 将 `FILE*` 替换为 `vfs_file_t*`
2. ✅ `fopen/fread/fseek` → `vfs_open/vfs_read/vfs_seek`
3. ✅ 保持API兼容,外部无感知

### Phase 3: BleClient适配
1. ✅ 实现 `TxtPageManager` 类
2. ✅ 监听页面请求事件
3. ✅ 自动预加载机制

### Phase 4: 测试验证
1. ✅ 本地文件读取功能不变
2. ✅ BLE阅读流畅度测试
3. ✅ 内存占用监控

---

## 💡 优势总结

### ✅ 统一接口
- 本地/BLE使用相同代码逻辑
- 减少重复代码50%+

### ✅ 智能缓存
- 预加载机制,翻页无延迟
- LRU淘汰,内存可控

### ✅ 容错机制
- 网络不稳定时优雅降级
- 部分加载总比完全失败好

### ✅ 可扩展
- 未来可支持HTTP、WebDAV等远程源
- 只需实现新的vfs_xxx_impl.c

---

## 📈 预期效果

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 翻页响应时间 | 500-2000ms | <50ms (缓存命中) |
| 内存占用 | 不可控 | 5-10页×1KB ≈ 10KB |
| 网络请求次数 | 每页1次 | 窗口预加载,减少80% |
| 代码复杂度 | 双重逻辑 | 统一抽象,降低40% |

---

## ⚠️ 注意事项

1. **初次加载**: 第一次打开BLE书籍需等待初始窗口下载
2. **内存限制**: 窗口大小需根据ESP32可用内存调整
3. **协议同步**: ESP32和BleClient的分页大小需一致
4. **错误处理**: 网络断开时需有重试机制

---

## 🔗 相关文档

- [BLE_SLIDING_WINDOW_PROTOCOL.md](../../BleReadBook/BleClient/BLE_SLIDING_WINDOW_PROTOCOL.md)
- [TXT_READER_ANALYSIS.md](./TXT_READER_ANALYSIS.md)
- [ble_cache_manager.h](../main/ui/ble/ble_cache_manager.h)
