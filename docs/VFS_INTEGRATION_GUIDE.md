# 虚拟文件系统集成指南

## 快速开始

### ESP32端集成

#### 1. 修改txt_reader.c使用VFS

```c
// 原来的代码
FILE *fp = fopen("/sdcard/book.txt", "rb");
fread(buffer, 1, size, fp);
fclose(fp);

// 改为VFS
#include "vfs/vfs_reader.h"

vfs_file_t *file = vfs_open("/sdcard/book.txt");  // 本地文件
// 或
vfs_file_t *file = vfs_open("ble://my_book");      // BLE远程文件

vfs_read(file, buffer, size);
vfs_close(file);
```

#### 2. 在ble_reader_screen.c中接收页面数据

```c
#include "vfs/vfs_reader.h"

// 全局BLE文件句柄
static vfs_file_t *s_ble_book_file = NULL;

// 处理X4IM协议接收到的TXT页面
static void handle_ble_txt_page(const char *filename, const uint8_t *data, size_t len) {
    // 解析文件名: "page_123" -> page_index = 123
    int page_index = 0;
    if (sscanf(filename, "page_%d", &page_index) != 1) {
        ESP_LOGW(TAG, "Invalid filename format: %s", filename);
        return;
    }
    
    // 获取BLE缓存句柄
    extern bool vfs_ble_receive_page(void *cache, int page_index, 
                                     const char *content, size_t content_len);
    
    if (s_ble_book_file && s_ble_book_file->source == VFS_SOURCE_BLE) {
        vfs_ble_receive_page(s_ble_book_file->handle, page_index, 
                            (const char*)data, len);
        
        ESP_LOGI(TAG, "Received BLE page %d (%zu bytes)", page_index, len);
        
        // 触发重绘(如果是当前页)
        refresh_display_if_needed(page_index);
    }
}

// 打开BLE书籍
void ble_reader_open_book(const char *book_id) {
    // 关闭旧文件
    if (s_ble_book_file) {
        vfs_close(s_ble_book_file);
    }
    
    // 打开新文件
    char identifier[128];
    snprintf(identifier, sizeof(identifier), "ble://%s", book_id);
    
    s_ble_book_file = vfs_open(identifier);
    if (!s_ble_book_file) {
        ESP_LOGE(TAG, "Failed to open BLE book: %s", book_id);
        return;
    }
    
    // 设置预加载窗口
    vfs_set_prefetch_window(s_ble_book_file, 5);
    
    ESP_LOGI(TAG, "BLE book opened: %s", book_id);
}

// 读取文本显示
void ble_reader_display_page(void) {
    if (!s_ble_book_file) {
        return;
    }
    
    char buffer[2048];
    int bytes = vfs_read(s_ble_book_file, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        display_text(buffer);
    } else {
        display_message("加载中...");
    }
}
```

### BleClient端集成

#### 1. 在main.js中添加监听

```javascript
import { TxtPageManager, setGlobalTxtManager } from './txt_page_manager.js';

// 全局变量
let currentTxtManager = null;

// 监听ESP32的页面请求
notifyCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
  const decoder = new TextDecoder();
  const message = decoder.decode(event.target.value);
  
  // 处理页面请求: "PAGE:123"
  if (message.startsWith('PAGE:')) {
    if (currentTxtManager) {
      currentTxtManager.handlePageRequest(message);
    } else {
      console.warn('[BleClient] No TXT manager available for request:', message);
    }
  }
});
```

#### 2. 开始阅读

```javascript
// 用户选择txt文件或从服务器获取文本
async function startReading(txtContent) {
  // 创建分页管理器
  currentTxtManager = new TxtPageManager(txtContent, 1024);
  
  // 发送初始窗口 (page 0及相邻页)
  await currentTxtManager.initializeReading(0, 3);
  
  // 显示统计
  console.log('TXT Manager Stats:', currentTxtManager.getStats());
  
  // 保存到全局
  setGlobalTxtManager(currentTxtManager);
}

// 示例: 从微信读书获取文本
async function readWeReadBook() {
  const response = await fetch('http://localhost:8080/api/weread/page', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      url: 'https://weread.qq.com/web/reader/...',
      action: 'page_current'
    })
  });
  
  const data = await response.json();
  const txtContent = data.text || data.content;
  
  await startReading(txtContent);
}
```

---

## 完整示例

### 场景1: 读取本地TXT文件 (ESP32)

```c
#include "vfs/vfs_reader.h"
#include "display_engine.h"

void read_local_txt_book(void) {
    // 打开本地文件
    vfs_file_t *file = vfs_open("/sdcard/books/my_novel.txt");
    if (!file) {
        display_message("无法打开文件");
        return;
    }
    
    // 读取并显示
    char page_buffer[2048];
    while (!vfs_eof(file)) {
        int bytes = vfs_read(file, page_buffer, sizeof(page_buffer) - 1);
        if (bytes > 0) {
            page_buffer[bytes] = '\0';
            display_text(page_buffer);
            
            // 等待用户翻页
            wait_for_page_turn();
        }
    }
    
    vfs_close(file);
}
```

### 场景2: 读取BLE远程TXT (ESP32 + BleClient)

**ESP32端:**

```c
#include "vfs/vfs_reader.h"

static vfs_file_t *s_remote_book = NULL;

void start_ble_reading(uint32_t book_hash, uint32_t chapter_hash) {
    // 打开BLE虚拟文件
    char identifier[64];
    snprintf(identifier, sizeof(identifier), "ble://%08lx/%08lx", 
             book_hash, chapter_hash);
    
    s_remote_book = vfs_open(identifier);
    if (!s_remote_book) {
        ESP_LOGE(TAG, "Failed to open BLE book");
        return;
    }
    
    // 设置大窗口 (如果内存充足)
    vfs_set_prefetch_window(s_remote_book, 8);
    
    display_first_page();
}

void display_first_page(void) {
    char buffer[2048];
    
    // 从位置0开始读取
    vfs_seek(s_remote_book, 0, SEEK_SET);
    int bytes = vfs_read(s_remote_book, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        display_text(buffer);
    } else {
        display_message("等待加载...");
    }
}

void next_page(void) {
    // 直接读取下一页 (VFS自动处理缓存)
    char buffer[2048];
    int bytes = vfs_read(s_remote_book, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        display_text(buffer);
        
        // 打印缓存统计
        uint32_t hits, misses;
        if (vfs_get_cache_stats(s_remote_book, &hits, &misses)) {
            ESP_LOGI(TAG, "Cache: %lu hits, %lu misses (%.1f%%)", 
                     hits, misses, 
                     (float)hits / (hits + misses) * 100);
        }
    }
}

// 在X4IM协议处理中接收页面
void handle_x4im_txt_file(const char *filename, const uint8_t *data, size_t len) {
    int page_index;
    if (sscanf(filename, "page_%d", &page_index) == 1) {
        extern bool vfs_ble_receive_page(void*, int, const char*, size_t);
        
        if (s_remote_book && s_remote_book->source == VFS_SOURCE_BLE) {
            vfs_ble_receive_page(s_remote_book->handle, page_index, 
                                (const char*)data, len);
        }
    }
}
```

**BleClient端:**

```javascript
import { TxtPageManager } from './txt_page_manager.js';

let txtManager = null;

// 初始化
async function initBleReading() {
  // 从服务器获取文本
  const txtContent = await fetchBookContent();
  
  // 创建管理器
  txtManager = new TxtPageManager(txtContent, 1024);
  
  // 发送初始3页
  await txtManager.initializeReading(0, 2);
}

// 监听请求
notifyCharacteristic.addEventListener('characteristicvaluechanged', async (event) => {
  const message = new TextDecoder().decode(event.target.value);
  
  if (message.startsWith('PAGE:') && txtManager) {
    await txtManager.handlePageRequest(message);
  }
});
```

---

## 性能优化建议

### 1. 动态调整窗口大小

```c
// 根据可用内存动态调整
void adjust_prefetch_window(vfs_file_t *file) {
    size_t free_heap = esp_get_free_heap_size();
    int window_size;
    
    if (free_heap > 200 * 1024) {
        window_size = 10;  // 内存充足
    } else if (free_heap > 100 * 1024) {
        window_size = 5;   // 中等内存
    } else {
        window_size = 3;   // 低内存
    }
    
    vfs_set_prefetch_window(file, window_size);
}
```

### 2. 监控缓存命中率

```c
void print_cache_stats(vfs_file_t *file) {
    uint32_t hits, misses;
    if (vfs_get_cache_stats(file, &hits, &misses)) {
        float hit_rate = (float)hits / (hits + misses) * 100;
        
        ESP_LOGI(TAG, "Cache stats: %lu hits, %lu misses (%.1f%% hit rate)",
                 hits, misses, hit_rate);
        
        // 如果命中率低于50%,增大窗口
        if (hit_rate < 50.0f) {
            ESP_LOGW(TAG, "Low cache hit rate, consider increasing window size");
        }
    }
}
```

### 3. 压缩传输 (可选)

```javascript
// BleClient: 压缩页面内容
import pako from 'pako';

async sendCompressedPage(pageIndex) {
  const pageData = this.getPage(pageIndex);
  const compressed = pako.deflate(pageData.bytes);
  
  await sendFileToDeviceWithProgress({
    type: 0x04,
    filename: `page_${pageIndex}`,
    data: compressed,
    flags: X4IM_FLAGS.TYPE_TXT | X4IM_FLAGS.COMPRESS
  });
}
```

```c
// ESP32: 解压页面
#include "miniz.h"

bool decompress_and_cache_page(int page_index, const uint8_t *compressed, size_t comp_len) {
    uint8_t *decompressed = malloc(BLE_PAGE_SIZE);
    size_t decomp_len = BLE_PAGE_SIZE;
    
    if (mz_uncompress(decompressed, &decomp_len, compressed, comp_len) == MZ_OK) {
        vfs_ble_receive_page(cache, page_index, (char*)decompressed, decomp_len);
        free(decompressed);
        return true;
    }
    
    free(decompressed);
    return false;
}
```

---

## 故障排查

### 问题1: 页面加载超时

**症状**: 翻页时一直显示"加载中..."

**解决**:
1. 检查BLE连接是否正常
2. 检查BleClient是否收到PAGE请求
3. 增大超时时间: `BLE_PAGE_TIMEOUT_MS`
4. 查看ESP32日志: `ESP_LOGE(TAG, "Page %d load timeout")`

### 问题2: 缓存命中率低

**症状**: 频繁请求页面,翻页慢

**解决**:
1. 增大预加载窗口: `vfs_set_prefetch_window(file, 8)`
2. 检查BleClient预加载逻辑是否工作
3. 确认页面大小合理 (1KB建议值)

### 问题3: 内存不足

**症状**: 系统崩溃或malloc失败

**解决**:
1. 减小窗口大小: `vfs_set_prefetch_window(file, 3)`
2. 减小页面大小: `new TxtPageManager(text, 512)`
3. 启用LRU淘汰 (已自动启用)
4. 定期清理缓存: `vfs_clear_cache(file)`

---

## API快速参考

### ESP32 VFS API

```c
vfs_file_t* vfs_open(const char *identifier);
int vfs_read(vfs_file_t *file, void *buffer, size_t size);
bool vfs_seek(vfs_file_t *file, long offset, int whence);
long vfs_tell(vfs_file_t *file);
void vfs_close(vfs_file_t *file);
bool vfs_set_prefetch_window(vfs_file_t *file, int window_size);
bool vfs_get_cache_stats(vfs_file_t *file, uint32_t *hits, uint32_t *misses);
```

### BleClient TxtPageManager API

```javascript
const manager = new TxtPageManager(txtContent, pageSize);
await manager.sendPage(pageIndex);
await manager.prefetchWindow(centerPage, windowSize);
await manager.handlePageRequest("PAGE:123");
await manager.initializeReading(startPage);
manager.getStats();
```

---

## 下一步

1. ✅ 将txt_reader.c迁移到VFS
2. ✅ 在ble_reader_screen.c中集成页面接收
3. ✅ BleClient添加TxtPageManager
4. ✅ 测试本地文件兼容性
5. ✅ 测试BLE远程阅读流畅度
6. ✅ 性能调优和内存优化
