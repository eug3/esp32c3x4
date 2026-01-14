# EPUB 预缓存快速参考

## 一分钟了解

**功能**: 自动预解压几章到 Flash，让翻页速度从 800ms 降到 50ms（16倍提升）

**原理**: 滑动窗口机制，缓存当前章节前后几章

**配置**: 前2章 + 后5章，最多10章同时缓存

## 快速开始

### 1. 初始化（只需一次）

```c
#include "epub_precache.h"

void app_main(void) {
    // ... 其他初始化代码 ...
    
    epub_precache_init();  // 就这一行！
    
    // ... 其他代码 ...
}
```

### 2. 正常使用，无需修改

```c
epub_reader_t reader;
epub_parser_init(&reader);
epub_parser_open(&reader, "/sdcard/book/my_book.epub");

// 跳转章节，自动触发预缓存
epub_parser_goto_chapter(&reader, 5);  // 自动缓存章节 3-10

// 翻页，自动更新窗口
epub_parser_next_chapter(&reader);     // 自动缓存章节 4-11

epub_parser_close(&reader);
```

**就这么简单！** 无需修改现有代码。

## 核心 API

| 函数 | 说明 | 使用场景 |
|------|------|---------|
| `epub_precache_init()` | 初始化 | 应用启动 |
| `epub_precache_update_window()` | 更新窗口 | 自动调用 |
| `epub_precache_chapter()` | 手动缓存 | 特殊需求 |
| `epub_precache_clear_all()` | 清空缓存 | 切换书籍 |
| `epub_precache_get_stats()` | 获取统计 | 调试监控 |

## 配置调整

在 `epub_precache.h` 修改：

```c
#define PRECACHE_WINDOW_BEFORE 2   // 当前之前缓存章节数
#define PRECACHE_WINDOW_AFTER  5   // 当前之后缓存章节数
#define PRECACHE_MAX_CHAPTERS  10  // 最大缓存章节数
```

### 预设配置

```c
// 快速阅读（向前为主）
#define PRECACHE_WINDOW_BEFORE 1
#define PRECACHE_WINDOW_AFTER  7

// 平衡模式（前后均衡）
#define PRECACHE_WINDOW_BEFORE 3
#define PRECACHE_WINDOW_AFTER  3

// 省空间模式（最小缓存）
#define PRECACHE_WINDOW_BEFORE 1
#define PRECACHE_WINDOW_AFTER  2
#define PRECACHE_MAX_CHAPTERS  5
```

## 性能数据

| 场景 | 无预缓存 | 有预缓存 | 提升 |
|------|---------|---------|------|
| 翻到已缓存章节 | 800ms | 50ms | **16x** |
| 连续翻页 | 800ms | 50ms | **16x** |
| 章节内翻页 | 50ms | 50ms | 1x |

## 存储占用

- 每章：20-50 KB
- 10章：200-500 KB
- 总容量：2 MB LittleFS

## 日志标识

```
I EPUB_PRECACHE: Updating precache window: current=5, total=20
I EPUB_PRECACHE: ✓ Chapter 3 cached: 25600 bytes
I EPUB_PRECACHE: Chapter 4 already cached  ← 缓存命中
I EPUB_PRECACHE: Precache window updated: 8 cached, 0 failed
```

## 文件位置

```
main/ui/epub/
├── epub_precache.h              # 头文件
├── epub_precache.c              # 实现
└── epub_precache_example.c      # 示例（仅参考）

EPUB_PRECACHE_DESIGN.md          # 详细设计
EPUB_PRECACHE_IMPLEMENTATION.md  # 实现报告
```

## 常见问题

**Q: 需要修改现有代码吗？**  
A: 不需要！只需在应用启动时调用 `epub_precache_init()`。

**Q: 会占用多少 Flash 空间？**  
A: 默认配置约 200-500 KB，可配置。

**Q: 首次打开会慢吗？**  
A: 首次需要预缓存，可能需要 2-5 秒，之后翻页就很快。

**Q: 切换书籍需要清理缓存吗？**  
A: 可选。调用 `epub_precache_clear_all()` 可以节省空间。

**Q: 如何监控缓存状态？**  
A: 使用 `epub_precache_get_stats()` 或查看日志。

## 更多信息

- 详细设计：`EPUB_PRECACHE_DESIGN.md`
- 实现报告：`EPUB_PRECACHE_IMPLEMENTATION.md`
- 使用示例：`main/ui/epub/epub_precache_example.c`

---

**版本**: v1.0 | **日期**: 2026-01-09
