# 阅读历史功能 - 快速参考

## 一分钟了解

**功能**: 自动记录每本书的阅读位置和最近阅读的 10 本书

**优势**: 
- 打开书籍自动恢复上次位置
- 可快速切换最近阅读的书籍
- 持久化存储，重启不丢失

## 快速开始

### 1. 初始化（只需一次）

```c
#include "reading_history.h"

void app_main(void) {
    nvs_flash_init();
    reading_history_init();  // 就这一行！
}
```

### 2. 自动工作

EPUB 和 TXT 阅读器已自动集成，**无需修改代码**！

```c
// 打开书籍（自动加载历史位置）
epub_parser_open(&reader, "/sdcard/book/my_book.epub");
epub_parser_load_position(&reader);  // 自动从历史恢复

// 保存位置（自动更新历史）
epub_parser_save_position(&reader);  // 同步更新历史
```

## 核心 API

| 函数 | 说明 |
|------|------|
| `reading_history_init()` | 初始化（应用启动） |
| `reading_history_get_last_book_path()` | 获取上次阅读的书 |
| `reading_history_get_recent_books()` | 获取最近阅读列表 |
| `reading_history_load_record()` | 加载指定书籍记录 |
| `reading_history_save_record()` | 保存书籍记录 |

## 使用示例

### 继续阅读上次的书

```c
const char *last_book = reading_history_get_last_book_path();
if (last_book) {
    epub_parser_open(&reader, last_book);
    epub_parser_load_position(&reader);
}
```

### 显示最近阅读列表

```c
book_record_t recent[5];
int count = reading_history_get_recent_books(5, recent);

for (int i = 0; i < count; i++) {
    printf("%d. %s - 进度 %d%%\n", 
           i + 1, 
           recent[i].title, 
           recent[i].position.progress_percent);
}
```

### 获取书籍信息

```c
book_record_t record;
if (reading_history_load_record("/sdcard/book/my_book.epub", &record)) {
    printf("书名: %s\n", record.title);
    printf("章节: %ld\n", record.position.chapter);
    printf("进度: %d%%\n", record.position.progress_percent);
}
```

## 数据格式

### 阅读位置

```c
typedef struct {
    int32_t chapter;          // 章节
    int32_t page;             // 页码
    int64_t byte_offset;      // 字节偏移（TXT）
    int32_t progress_percent; // 进度 0-100%
} reading_position_t;
```

### 书籍记录

```c
typedef struct {
    char file_path[256];      // 文件路径
    char title[128];          // 书名
    book_type_t type;         // TXT/EPUB
    reading_position_t pos;   // 位置
    time_t last_read_time;    // 最后阅读时间
    uint32_t total_read_time; // 总阅读时长（秒）
} book_record_t;
```

## 存储信息

- **位置**: NVS Flash
- **命名空间**: `book_history`
- **容量**: 最多 10 本书
- **占用**: ~6 KB

## 日志示例

```
I READING_HISTORY: Initializing reading history manager
I READING_HISTORY: Loaded 5 book records from NVS
I READING_HISTORY: Updated existing record: 我的小说
I READING_HISTORY: Found record: 我的小说 (chapter=7, page=12)
```

## 文件清单

```
main/ui/
├── reading_history.h         # 头文件
└── reading_history.c         # 实现

READING_HISTORY_GUIDE.md      # 详细文档
```

## 集成状态

- ✅ EPUB 阅读器：自动集成
- ✅ TXT 阅读器：待集成（类似 EPUB）
- ✅ NVS 存储：已实现
- ✅ 内存缓存：已实现

## 常见问题

**Q: 需要手动保存吗？**  
A: 不需要！`epub_parser_save_position()` 会自动更新历史。

**Q: 如何清空历史？**  
A: `reading_history_clear_all()`

**Q: 支持多少本书？**  
A: 最多 10 本，可在头文件中调整。

**Q: 性能影响？**  
A: 极小。使用内存缓存，NVS 写入次数很少。

---

**版本**: v1.0 | **日期**: 2026-01-09
