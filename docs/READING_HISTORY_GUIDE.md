# 阅读历史管理功能

## 功能概述

实现了完整的阅读历史管理系统，可以：
- ✅ 记住每本书的阅读位置（章节、页码、字节偏移）
- ✅ 维护最近阅读的 10 本书列表
- ✅ 自动记录阅读时间和进度百分比
- ✅ 快速恢复上次阅读位置
- ✅ 持久化存储到 NVS Flash

## 核心特性

### 1. 自动记录阅读位置

每次跳转章节或翻页时，自动保存：
- 当前章节索引
- 当前页码
- 字节偏移（TXT 文件）
- 阅读进度百分比
- 最后阅读时间

### 2. 最近阅读列表

维护最多 10 本最近阅读的书籍：
- 按最后阅读时间排序
- 自动淘汰最旧的记录
- 重新打开的书自动移到列表最前

### 3. 智能恢复

打开书籍时：
1. 优先从阅读历史加载位置
2. 回退到旧的 NVS 位置记录
3. 如果都没有，从第一章开始

## 使用方法

### 初始化

在应用启动时调用一次：

```c
#include "reading_history.h"

void app_main(void) {
    // 初始化 NVS
    nvs_flash_init();
    
    // 初始化阅读历史
    reading_history_init();
    
    // ... 其他代码 ...
}
```

### 自动集成

EPUB 和 TXT 阅读器已自动集成，无需修改代码！

```c
// 打开书籍（自动加载历史位置）
epub_reader_t reader;
epub_parser_init(&reader);
epub_parser_open(&reader, "/sdcard/book/my_book.epub");

// 自动加载上次阅读位置
epub_parser_load_position(&reader);  // 优先从阅读历史读取

// 跳转章节（自动保存到历史）
epub_parser_goto_chapter(&reader, 5);

// 保存位置（自动更新历史）
epub_parser_save_position(&reader);  // 同步更新阅读历史
```

### 获取最近阅读的书籍

```c
book_record_t recent_books[5];
int count = reading_history_get_recent_books(5, recent_books);

for (int i = 0; i < count; i++) {
    printf("书名: %s\n", recent_books[i].title);
    printf("类型: %s\n", reading_history_get_type_string(recent_books[i].type));
    printf("进度: %d%%\n", recent_books[i].position.progress_percent);
    printf("章节: %ld/%ld\n", 
           recent_books[i].position.chapter + 1, 
           /* 总章节数需要从元数据获取 */);
    
    char time_str[64];
    reading_history_format_time(recent_books[i].last_read_time, time_str, sizeof(time_str));
    printf("最后阅读: %s\n", time_str);
    printf("---\n");
}
```

### 快速打开上次阅读的书

```c
const char *last_book = reading_history_get_last_book_path();
if (last_book) {
    printf("继续阅读: %s\n", last_book);
    
    // 加载书籍
    epub_reader_t reader;
    epub_parser_init(&reader);
    epub_parser_open(&reader, last_book);
    epub_parser_load_position(&reader);  // 恢复位置
}
```

### 手动管理阅读记录

```c
// 创建新记录
book_record_t record = reading_history_create_record(
    "/sdcard/book/my_novel.epub",
    "我的小说",
    BOOK_TYPE_EPUB
);

// 设置阅读位置
record.position.chapter = 5;
record.position.page = 10;
record.position.progress_percent = 25;

// 保存记录
reading_history_save_record(&record);

// 加载记录
book_record_t loaded;
if (reading_history_load_record("/sdcard/book/my_novel.epub", &loaded)) {
    printf("找到记录: %s, 章节 %ld\n", loaded.title, loaded.position.chapter);
}

// 只更新位置
reading_position_t new_pos = {
    .chapter = 7,
    .page = 0,
    .progress_percent = 35
};
reading_history_update_position("/sdcard/book/my_novel.epub", &new_pos);

// 标记为已读（更新阅读时长）
reading_history_mark_as_read("/sdcard/book/my_novel.epub", 3600);  // 读了1小时

// 删除记录
reading_history_delete_record("/sdcard/book/my_novel.epub");

// 清空所有历史
reading_history_clear_all();
```

## 数据结构

### book_record_t

```c
typedef struct {
    char file_path[256];         // 文件完整路径
    char title[128];             // 书名
    book_type_t type;            // 书籍类型（TXT/EPUB）
    reading_position_t position; // 阅读位置
    time_t last_read_time;       // 最后阅读时间
    uint32_t total_read_time;    // 总阅读时长（秒）
    bool is_valid;               // 记录是否有效
} book_record_t;
```

### reading_position_t

```c
typedef struct {
    int32_t chapter;          // 当前章节
    int32_t page;             // 当前页码
    int64_t byte_offset;      // 字节偏移（TXT）
    int32_t progress_percent; // 进度百分比（0-100）
} reading_position_t;
```

## 存储架构

### NVS 命名空间

- **命名空间**: `book_history`
- **键格式**:
  - `count`: 记录数量
  - `book_0`, `book_1`, ..., `book_9`: 书籍记录（blob）

### 内存缓存

为了减少 NVS 读写次数，系统维护了一个内存缓存：
- 首次访问时加载所有记录到内存
- 所有操作先更新内存缓存
- 然后同步到 NVS

### 存储容量

每条记录约 600 字节，最多 10 条：
- 总占用: ~6 KB NVS 空间
- 足够存储详细的阅读信息

## 工作流程

### 打开书籍

```
epub_parser_open()
    ↓
检查阅读历史
    ↓
有记录？
    ├─ 是 → 加载元数据
    └─ 否 → 创建新记录
    ↓
返回打开成功
```

### 加载位置

```
epub_parser_load_position()
    ↓
1. 优先从 reading_history 加载
    ├─ 找到 → 恢复位置
    └─ 未找到 → 继续
    ↓
2. 回退到旧 NVS 方式
    ├─ 找到 → 恢复位置
    └─ 未找到 → 从头开始
```

### 保存位置

```
epub_parser_save_position()
    ↓
1. 保存到旧 NVS（兼容）
    ↓
2. 更新阅读历史记录
    ├─ 更新位置
    ├─ 更新进度百分比
    ├─ 更新最后阅读时间
    └─ 移到列表最前
    ↓
3. 同步到 NVS
```

## 界面集成建议

### 主界面显示

```c
// 显示"继续阅读"按钮
const char *last_book = reading_history_get_last_book_path();
if (last_book) {
    book_record_t record;
    reading_history_load_record(last_book, &record);
    
    // 显示书名和进度
    display_text("继续阅读: %s", record.title);
    display_text("进度: %d%%", record.position.progress_percent);
}
```

### 历史列表界面

```c
void show_reading_history(void) {
    book_record_t books[10];
    int count = reading_history_get_recent_books(10, books);
    
    for (int i = 0; i < count; i++) {
        char time_str[64];
        reading_history_format_time(books[i].last_read_time, 
                                    time_str, sizeof(time_str));
        
        printf("%d. %s [%s]\n", i + 1, books[i].title, 
               reading_history_get_type_string(books[i].type));
        printf("   进度: %d%% | 最后阅读: %s\n", 
               books[i].position.progress_percent, time_str);
        printf("   位置: 第%ld章 第%ld页\n\n",
               books[i].position.chapter + 1, 
               books[i].position.page + 1);
    }
}
```

### 快速跳转菜单

```c
void quick_resume_menu(void) {
    book_record_t recent[5];
    int count = reading_history_get_recent_books(5, recent);
    
    printf("最近阅读:\n");
    for (int i = 0; i < count; i++) {
        printf("%d. %s (%d%%)\n", i + 1, 
               recent[i].title, 
               recent[i].position.progress_percent);
    }
    
    // 用户选择后：
    int choice = get_user_choice();
    if (choice >= 0 && choice < count) {
        open_book_and_resume(recent[choice].file_path);
    }
}
```

## 性能优化

### 缓存策略

1. **内存缓存**: 首次加载后保存在 RAM
2. **延迟写入**: 可批量更新后一次性写 NVS
3. **增量更新**: 只写变化的记录

### 节省 NVS 寿命

```c
// 不要频繁保存（如每次翻页）
// 建议：退出时保存、切换章节时保存

// ❌ 不好
void on_page_turn() {
    epub_parser_save_position(&reader);  // 每次翻页都写 NVS
}

// ✅ 好
void on_chapter_change() {
    epub_parser_save_position(&reader);  // 只在换章节时写
}

void on_exit() {
    epub_parser_save_position(&reader);  // 退出时保存
}
```

## 调试和监控

### 日志输出

```
I (12345) READING_HISTORY: Initializing reading history manager
I (12350) READING_HISTORY: Loaded 5 book records from NVS
I (12400) READING_HISTORY: Updated existing record: 我的小说
I (12450) READING_HISTORY: Added new record: 新书 (6 total)
I (12500) READING_HISTORY: Found record: 我的小说 (chapter=7, page=12)
```

### 查看所有记录

```c
void dump_reading_history(void) {
    reading_history_t history;
    if (reading_history_load_all(&history)) {
        printf("=== 阅读历史 (%d 本) ===\n", history.count);
        
        for (int i = 0; i < history.count; i++) {
            book_record_t *rec = &history.books[i];
            
            char time_str[64];
            reading_history_format_time(rec->last_read_time, 
                                       time_str, sizeof(time_str));
            
            printf("\n书名: %s\n", rec->title);
            printf("路径: %s\n", rec->file_path);
            printf("类型: %s\n", reading_history_get_type_string(rec->type));
            printf("位置: 章节%ld 页%ld (进度%d%%)\n", 
                   rec->position.chapter, 
                   rec->position.page,
                   rec->position.progress_percent);
            printf("最后阅读: %s\n", time_str);
            printf("总阅读: %u 分钟\n", rec->total_read_time / 60);
        }
    }
}
```

## 常见问题

**Q: 如果删除了书籍文件，历史记录会怎样？**  
A: 历史记录仍然存在。可以选择：
   - 手动删除该记录
   - 打开书籍时检查文件是否存在，自动清理无效记录

**Q: 更换书籍位置后还能识别吗？**  
A: 不能。记录基于完整路径。如果移动文件，需要重新创建记录。

**Q: 如何避免 NVS 空间占满？**  
A: 限制最多 10 本书。如果需要更多，可以调整 `READING_HISTORY_MAX_BOOKS`。

**Q: 支持同步到云端吗？**  
A: 当前只支持本地 NVS。可以扩展实现云同步功能。

**Q: TXT 和 EPUB 混合管理吗？**  
A: 是的，统一管理。通过 `book_type_t` 区分类型。

## 未来改进

- [ ] 自动清理不存在的书籍记录
- [ ] 支持书签功能（每本书多个位置）
- [ ] 阅读统计（每日阅读时长、完成书籍数）
- [ ] 云同步支持
- [ ] 导出/导入阅读历史
- [ ] 阅读笔记和标注

---

**版本**: v1.0  
**日期**: 2026-01-09  
**作者**: GitHub Copilot
