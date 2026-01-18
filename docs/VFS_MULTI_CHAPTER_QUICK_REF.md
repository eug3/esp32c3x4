# VFS多章节系统 - 快速参考

## 🎯 核心功能
**支持从BleClient读取服务器下载的多个章节txt文本**

---

## 📦 新增文件

### ESP32端
- ✅ `vfs_reader.h` - 新增章节管理API
- ✅ `vfs_ble_impl.c` - 支持章节索引
- ✅ `vfs_multi_chapter_example.c` - 完整示例

### BleClient端  
- ✅ `multi_chapter_manager.js` - 多章节管理器

---

## ⚡ 快速使用

### ESP32 (C)

```c
#include "vfs/vfs_reader.h"

// 1. 打开书籍
vfs_file_t *book = vfs_open("ble://my_novel");

// 2. 设置总章节数
vfs_set_total_chapters(book, 25);

// 3. 读取当前章节
char buf[2048];
vfs_read(book, buf, sizeof(buf));

// 4. 切换章节
vfs_switch_chapter(book, 5);  // 切到第6章

// 5. 查询信息
int current = vfs_get_current_chapter(book);  // => 5
int total = vfs_get_total_chapters(book);     // => 25
```

### BleClient (JS)

```javascript
import { initializeMultiChapterReading } from './multi_chapter_manager.js';

// 1. 初始化
const mgr = await initializeMultiChapterReading(
  'https://weread.qq.com/web/reader/abc123',  // 书籍URL
  25,  // 总章节数
  { pageSize: 1024, preloadCount: 3 }
);

// 2. 监听请求 (新格式: PAGE:chapter:page)
notifyChar.addEventListener('characteristicvaluechanged', async (e) => {
  const msg = new TextDecoder().decode(e.target.value);
  if (msg.startsWith('PAGE:')) {
    await mgr.handlePageRequest(msg);  // 自动下载+发送
  }
});
```

---

## 🔄 协议变化

### 旧协议 (单章节)
```
ESP32 → BleClient: PAGE:123
BleClient → ESP32: page_123.txt
```

### 新协议 (多章节)
```
ESP32 → BleClient: PAGE:5:123      (第5章第123页)
BleClient → ESP32: page_123.txt    (内容为第5章第123页)
```

---

## 📋 完整API

### ESP32端

```c
// 章节切换
bool vfs_switch_chapter(vfs_file_t *file, int chapter_index);

// 查询章节
int vfs_get_current_chapter(vfs_file_t *file);
int vfs_get_total_chapters(vfs_file_t *file);

// 设置章节数
bool vfs_set_total_chapters(vfs_file_t *file, int total_chapters);

// 原有API不变
vfs_open(), vfs_read(), vfs_close()...
```

### BleClient端

```javascript
// MultiChapterManager类
class MultiChapterManager {
  // 加载章节
  loadChapterContent(chapterIndex, txtContent)
  async downloadChapter(chapterIndex)
  
  // 请求处理
  async handlePageRequest("PAGE:5:123")
  
  // 初始化
  async initializeReading(startChapter, windowSize)
  async preloadChapters(startChapter, count)
  
  // 查询
  getChapterInfo(chapterIndex)
  getStats()
}

// 快捷函数
await initializeMultiChapterReading(url, totalChapters, options)
```

---

## 💡 使用示例

### 完整阅读流程

**ESP32:**
```c
void read_book(void) {
    vfs_file_t *book = vfs_open("ble://weread");
    vfs_set_total_chapters(book, 20);
    
    for (int ch = 0; ch < 20; ch++) {
        vfs_switch_chapter(book, ch);
        
        char page[2048];
        while (vfs_read(book, page, sizeof(page)) > 0) {
            display(page);
            wait_input();
        }
    }
    
    vfs_close(book);
}
```

**BleClient:**
```javascript
const mgr = await initializeMultiChapterReading(
  'https://weread.qq.com/...',
  20,
  { preloadCount: 3 }
);

// 自动处理所有请求
setupBleListener(mgr);
```

---

## 📊 优势对比

| 功能 | 单章节 | 多章节 |
|------|--------|--------|
| 章节管理 | ❌ | ✅ |
| 自动下载 | ❌ | ✅ |
| 章节切换 | ❌ 需重开文件 | ✅ 一行代码 |
| 缓存策略 | 页级 | 章节+页级 |
| 内存占用 | 低 | 中 (可控) |

---

## 🔍 故障排查

**Q: 章节切换后无内容?**
```javascript
// 检查章节是否已下载
const info = mgr.getChapterInfo(5);
console.log(info.loaded);  // false则需下载
await mgr.downloadChapter(5);
```

**Q: 请求格式错误?**
```c
// 确保使用新格式 PAGE:ch:page
snprintf(req, sizeof(req), "PAGE:%d:%d", chapter, page);
```

---

## 📚 文档链接

- [VFS_MULTI_CHAPTER_GUIDE.md](./VFS_MULTI_CHAPTER_GUIDE.md) - 完整指南
- [vfs_multi_chapter_example.c](../examples/vfs_multi_chapter_example.c) - 示例代码
- [VFS_README.md](./VFS_README.md) - VFS总览

---

**立即开始**: 查看 [VFS_MULTI_CHAPTER_GUIDE.md](./VFS_MULTI_CHAPTER_GUIDE.md) 📖
