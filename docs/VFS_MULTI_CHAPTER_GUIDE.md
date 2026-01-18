# 多章节VFS系统使用指南

## 🎯 功能概述

扩展的VFS系统现在支持**多章节管理**，可以：
- ✅ 从BleClient读取服务器下载的多个章节txt文本
- ✅ ESP32端按章节索引切换和阅读
- ✅ 自动下载缺失章节
- ✅ 智能预加载和缓存

---

## 📦 核心文件

### ESP32端
- `vfs_reader.h` - 新增章节管理API
- `vfs_ble_impl.c` - 支持章节索引的缓存
- `vfs_reader.c` - 章节切换实现

### BleClient端
- `multi_chapter_manager.js` - 多章节管理器 ✅ 新增
- `txt_page_manager.js` - 单章节分页管理器

---

## 🚀 快速使用

### ESP32端 (C语言)

#### 1. 打开多章节书籍

```c
#include "vfs/vfs_reader.h"

// 打开BLE书籍
vfs_file_t *book = vfs_open("ble://weread_book_123");

// 设置总章节数 (从BleClient获知)
vfs_set_total_chapters(book, 25);  // 这本书有25章

// 查询章节信息
int total = vfs_get_total_chapters(book);  // => 25
int current = vfs_get_current_chapter(book);  // => 0 (第1章)

printf("Book has %d chapters, current: chapter %d\n", total, current);
```

#### 2. 读取当前章节

```c
// 读取第一章的内容
char buffer[2048];
int bytes = vfs_read(book, buffer, sizeof(buffer));

if (bytes > 0) {
    buffer[bytes] = '\0';
    display_text(buffer);  // 显示在屏幕上
}
```

#### 3. 切换章节

```c
// 切换到第5章 (索引4)
if (vfs_switch_chapter(book, 4)) {
    ESP_LOGI(TAG, "Switched to chapter 5");
    
    // 重新读取 (自动从新章节开头)
    int bytes = vfs_read(book, buffer, sizeof(buffer));
    display_text(buffer);
} else {
    ESP_LOGE(TAG, "Failed to switch chapter");
}
```

#### 4. 完整示例

```c
void read_multi_chapter_book(void) {
    // 打开书籍
    vfs_file_t *book = vfs_open("ble://my_novel");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    // 配置
    vfs_set_total_chapters(book, 10);  // 10章
    vfs_set_prefetch_window(book, 5);   // 预加载5页
    
    // 读取所有章节
    for (int chapter = 0; chapter < 10; chapter++) {
        // 切换章节
        if (!vfs_switch_chapter(book, chapter)) {
            ESP_LOGE(TAG, "Failed to switch to chapter %d", chapter);
            continue;
        }
        
        ESP_LOGI(TAG, "=== Reading Chapter %d ===", chapter + 1);
        
        // 读取章节内容
        char buffer[2048];
        while (!vfs_eof(book)) {
            int bytes = vfs_read(book, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) break;
            
            buffer[bytes] = '\0';
            display_page(buffer);
            
            // 等待用户翻页
            wait_for_input();
        }
    }
    
    // 关闭书籍
    vfs_close(book);
}
```

---

### BleClient端 (JavaScript)

#### 1. 初始化多章节管理器

```javascript
import { MultiChapterManager, initializeMultiChapterReading } from './multi_chapter_manager.js';

// 方式1: 快捷初始化
const manager = await initializeMultiChapterReading(
  'https://weread.qq.com/web/reader/abc123',  // 书籍URL
  25,  // 总章节数
  {
    pageSize: 1024,
    startChapter: 0,
    preloadCount: 3,  // 预下载前3章
    serverUrl: 'http://localhost:8080'
  }
);

// 方式2: 手动创建
const manager = new MultiChapterManager({
  bookUrl: 'https://weread.qq.com/web/reader/abc123',
  totalChapters: 25,
  pageSize: 1024,
  serverUrl: 'http://localhost:8080'
});

await manager.initializeReading(0);  // 从第0章开始
```

#### 2. 手动加载章节 (本地内容)

```javascript
// 如果已经有章节文本 (不需要从服务器下载)
const chapter0Text = "第一章的内容...";
const chapter1Text = "第二章的内容...";

manager.loadChapterContent(0, chapter0Text);
manager.loadChapterContent(1, chapter1Text);
```

#### 3. 监听ESP32请求

```javascript
// 监听蓝牙通知
notifyCharacteristic.addEventListener('characteristicvaluechanged', async (event) => {
  const message = new TextDecoder().decode(event.target.value);
  
  // 新格式: "PAGE:chapter:page"
  if (message.startsWith('PAGE:')) {
    await manager.handlePageRequest(message);
    // 自动下载章节 + 发送页面 + 预加载
  }
});
```

#### 4. 主动下载章节

```javascript
// 下载特定章节
const text = await manager.downloadChapter(5);
if (text) {
  console.log(`Chapter 5 downloaded: ${text.length} chars`);
}

// 批量预加载章节
await manager.preloadChapters(0, 5);  // 预加载前5章
```

#### 5. 查询状态

```javascript
// 全局统计
const stats = manager.getStats();
console.log(stats);
// => {
//   totalChapters: 25,
//   loadedChapters: 8,
//   cachedChapters: 8,
//   totalRequests: 156,
//   cacheHits: 142,
//   loadRate: '32.0%'
// }

// 单章节信息
const chapter5Info = manager.getChapterInfo(5);
console.log(chapter5Info);
// => {
//   loaded: true,
//   downloading: false,
//   totalPages: 45,
//   stats: { hitRate: '95.2%' }
// }
```

---

## 📊 协议格式

### ESP32 → BleClient 请求

```
旧格式: PAGE:123          (仅页码)
新格式: PAGE:5:123        (章节:页码)
        └─┬─┘ └┬┘ └─┬─┘
          │    │    └─ 页面索引
          │    └────── 章节索引
          └─────────── 命令
```

### BleClient → ESP32 响应

发送TXT文件,文件名格式不变:
```
filename: "page_123"
content: [章节5的第123页内容]
```

---

## 🎨 完整示例

### 场景: 阅读微信读书

**ESP32端:**

```c
void read_weread_book(void) {
    // 打开BLE书籍
    vfs_file_t *book = vfs_open("ble://weread_novel");
    
    // BleClient会发送总章节数 (或预先知道)
    vfs_set_total_chapters(book, 20);
    vfs_set_prefetch_window(book, 5);
    
    int current_chapter = 0;
    
    while (true) {
        // 读取当前页
        char page[2048];
        int bytes = vfs_read(book, page, sizeof(page) - 1);
        
        if (bytes > 0) {
            page[bytes] = '\0';
            display_text(page);
        } else {
            display_message("加载中...");
        }
        
        // 等待用户输入
        int key = wait_for_key();
        
        switch (key) {
            case KEY_NEXT_PAGE:
                // VFS自动请求下一页
                break;
                
            case KEY_PREV_PAGE:
                vfs_seek(book, -4096, SEEK_CUR);
                break;
                
            case KEY_NEXT_CHAPTER:
                current_chapter++;
                if (vfs_switch_chapter(book, current_chapter)) {
                    ESP_LOGI(TAG, "Next chapter");
                }
                break;
                
            case KEY_PREV_CHAPTER:
                current_chapter--;
                if (vfs_switch_chapter(book, current_chapter)) {
                    ESP_LOGI(TAG, "Previous chapter");
                }
                break;
                
            case KEY_EXIT:
                goto cleanup;
        }
    }
    
cleanup:
    vfs_close(book);
}
```

**BleClient端:**

```javascript
import { initializeMultiChapterReading, getGlobalMultiChapterManager } from './multi_chapter_manager.js';

async function startWeReadReading() {
  const bookUrl = 'https://weread.qq.com/web/reader/abc123def456';
  
  // 初始化 (自动从服务器下载第一章)
  const manager = await initializeMultiChapterReading(bookUrl, 20, {
    pageSize: 1024,
    preloadCount: 3,  // 预下载前3章
    serverUrl: 'http://localhost:8080'
  });
  
  console.log('Reading started!');
  console.log('Stats:', manager.getStats());
  
  // 监听ESP32请求
  setupBleListener(manager);
}

function setupBleListener(manager) {
  notifyCharacteristic.addEventListener('characteristicvaluechanged', async (event) => {
    const message = new TextDecoder().decode(event.target.value);
    
    if (message.startsWith('PAGE:')) {
      // 自动处理章节下载和页面发送
      await manager.handlePageRequest(message);
    }
  });
}

// 启动
startWeReadReading();
```

---

## 🔧 API参考

### ESP32端 C API

```c
// 章节管理
bool vfs_switch_chapter(vfs_file_t *file, int chapter_index);
int vfs_get_current_chapter(vfs_file_t *file);
int vfs_get_total_chapters(vfs_file_t *file);
bool vfs_set_total_chapters(vfs_file_t *file, int total_chapters);

// 示例
vfs_switch_chapter(book, 5);              // 切换到第6章
int ch = vfs_get_current_chapter(book);   // 获取当前章节
int total = vfs_get_total_chapters(book); // 获取总章节数
vfs_set_total_chapters(book, 25);         // 设置总章节数
```

### BleClient JS API

```javascript
// MultiChapterManager 类
class MultiChapterManager {
  loadChapterContent(chapterIndex, txtContent)    // 加载本地章节
  async downloadChapter(chapterIndex)             // 从服务器下载
  async getChapterManager(chapterIndex)           // 获取章节管理器
  async handlePageRequest(message)                // 处理请求
  async initializeReading(startChapter, windowSize)
  async preloadChapters(startChapter, count)
  getChapterInfo(chapterIndex)                    // 查询章节信息
  getStats()                                      // 全局统计
  clearAll()                                      // 清空缓存
}

// 全局函数
initializeMultiChapterReading(bookUrl, totalChapters, options)
setGlobalMultiChapterManager(manager)
getGlobalMultiChapterManager()
```

---

## 💡 最佳实践

### 1. 预加载策略

```javascript
// 智能预加载: 当前章节 + 前后各1章
async function smartPreload(currentChapter, totalChapters) {
  const toLoad = [];
  
  if (currentChapter > 0) toLoad.push(currentChapter - 1);
  toLoad.push(currentChapter);
  if (currentChapter < totalChapters - 1) toLoad.push(currentChapter + 1);
  
  for (const ch of toLoad) {
    await manager.downloadChapter(ch);
  }
}
```

### 2. 内存管理

```c
// ESP32: 定期清理缓存
void cleanup_old_chapters(vfs_file_t *book, int current_chapter) {
    // 只保留当前章节 ± 1
    // 其他章节的缓存会被自动清理
    
    // 切换章节时自动触发清理
    vfs_switch_chapter(book, current_chapter);
}
```

### 3. 错误处理

```javascript
// BleClient: 下载失败时重试
async function downloadWithRetry(manager, chapterIndex, maxRetries = 3) {
  for (let i = 0; i < maxRetries; i++) {
    try {
      const content = await manager.downloadChapter(chapterIndex);
      if (content) return content;
    } catch (error) {
      console.warn(`Retry ${i + 1}/${maxRetries}:`, error);
      await new Promise(r => setTimeout(r, 1000 * (i + 1)));
    }
  }
  return null;
}
```

---

## 📈 性能优化

### 减少延迟

```c
// 增大预加载窗口
vfs_set_prefetch_window(book, 8);  // 预加载8页

// 切换章节后立即预加载
vfs_switch_chapter(book, 5);
// VFS会自动预加载第5章的前几页
```

### 减少流量

```javascript
// 只在需要时下载
manager.downloadChapter(5);  // 按需下载第5章

// 避免一次性下载所有章节
// ❌ 不推荐
for (let i = 0; i < 25; i++) await manager.downloadChapter(i);

// ✅ 推荐: 按需 + 预加载
await manager.initializeReading(0);
await manager.preloadChapters(0, 3);  // 只预加载前3章
```

---

## 🐛 故障排查

### 问题1: 章节切换后无内容

**原因**: BleClient未下载该章节

**解决**:
```javascript
// 检查章节是否已加载
const info = manager.getChapterInfo(5);
if (!info.loaded) {
  console.log('Chapter 5 not loaded, downloading...');
  await manager.downloadChapter(5);
}
```

### 问题2: ESP32请求格式错误

**原因**: 使用了旧格式 `PAGE:123`

**解决**: 确保ESP32发送新格式 `PAGE:5:123`

```c
// 检查vfs_ble_impl.c中的request_page_from_client函数
snprintf(request, sizeof(request), "PAGE:%d:%d", 
         cache->current_chapter, page_index);
```

---

## 📚 相关文档

- [VFS_README.md](./VFS_README.md) - VFS系统总览
- [VFS_INTEGRATION_GUIDE.md](./VFS_INTEGRATION_GUIDE.md) - 基础集成
- [VIRTUAL_FILE_SYSTEM_DESIGN.md](./VIRTUAL_FILE_SYSTEM_DESIGN.md) - 架构设计

---

**开始使用多章节功能**: 查看上述示例代码立即开始! 📖✨
