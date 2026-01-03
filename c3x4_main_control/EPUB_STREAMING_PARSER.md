# EPUB 流式解析器实现说明

## 概述

为 ESP32-C3 (400KB RAM) 实现的轻量级 EPUB 流式解析器，参考 [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader) 项目，但针对内存受限环境进行了优化。

## 架构设计

### 三级缓存架构

```
┌─────────────────────────────────────────────────────────────┐
│                      内存层级                               │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  L1: RAM (400KB)        ← 当前正在阅读的内容                │
│  ├─ 文本缓冲区: ~50KB   ← 1-2 页文本                        │
│  ├─ LVGL 渲染: ~100KB  ← 显示缓冲区                         │
│  └─ 代码栈: ~150KB      ← 运行时栈                           │
│                                                              │
│  L2: Flash (16MB)        ← 热数据缓存                        │
│  ├─ 当前章节文本        ← 预解压的当前章节                   │
│  ├─ 章节索引            ← 快速跳转                           │
│  └─ 阅读进度            ← NVS                               │
│                                                              │
│  L3: SD 卡 (外部)       ← 冷存储                             │
│  └─ EPUB 原文件         ← 完整书籍                           │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 性能对比

| 存储类型 | 读取速度 (4KB) | 相对速度 | 适用场景 |
|---------|----------------|---------|---------|
| RAM (SRAM) | ~1 μs | 1x | 当前页内容 |
| Flash | ~200 μs | 200x | 章节缓存 |
| SD 卡 (SPI) | ~5-20 ms | 5000-20000x | EPUB 原文件 |

**Flash 比 SD 卡快 25-100 倍**

## 核心组件

### 1. ZIP 流式解析器 (`epub_zip.h/c`)

**功能**: 按需解压 EPUB 内的文件

**特点**:
- 只读取 ZIP 中心目录，不一次性加载整个文件
- 支持查找和解压单个文件
- 内存使用: ~4KB

**关键接口**:
```c
epub_zip_t* epub_zip_open(const char *epub_path);
bool epub_zip_find_file(epub_zip_t *zip, const char *filename, epub_zip_file_info_t *file_info);
int epub_zip_extract_file(epub_zip_t *zip, const epub_zip_file_info_t *file_info, void *buffer, size_t buffer_size);
```

### 2. XML 流式解析器 (`epub_xml.h/c`)

**功能**: 解析 EPUB 元数据 (content.opf)

**特点**:
- 手动解析，不依赖 TinyXML2（节省代码空间）
- 只提取需要的部分（metadata, spine, manifest）
- 内存使用: ~2KB

**关键接口**:
```c
epub_xml_parser_t* epub_xml_create(const char *xml_content, size_t content_length);
bool epub_xml_parse_metadata(epub_xml_parser_t *parser, epub_xml_metadata_t *metadata);
int epub_xml_parse_spine(epub_xml_parser_t *parser, epub_xml_spine_item_t *spine_items, int max_items);
```

### 3. HTML 流式解析器 (`epub_html.h/c`)

**功能**: 提取章节文本内容

**特点**:
- 逐块提取，不构建 DOM
- 支持基本标签（h1-h6, p, b, i, img）
- 内存使用: ~2KB

**关键接口**:
```c
epub_html_parser_t* epub_html_create(const char *html_content, size_t content_length);
bool epub_html_next_block(epub_html_parser_t *parser, epub_text_block_t *block);
```

### 4. 主解析器 (`epub_parser.c`)

**功能**: 协调各个组件，提供统一的 EPUB 阅读接口

**工作流程**:
```
1. 打开 EPUB 文件 (ZIP)
2. 解析 content.opf (XML)
   - 提取元数据（标题、作者）
   - 提取章节列表（spine + manifest）
3. 按需读取章节
   - 从 ZIP 中解压章节文件
   - 使用 HTML 解析器提取文本
4. 保存阅读进度 (NVS)
```

**内存使用**: ~8KB (不包括缓冲区)

### 5. Flash 缓存 (`epub_cache.h`)

**功能**: 使用 Flash 作为二级缓存

**设计思路**:
- 首次读取章节时，从 SD 卡解压并缓存到 Flash
- 后续读取直接从 Flash 获取（比 SD 卡快 25-100 倍）
- LRU 淘汰策略

**待实现**: 只定义了接口，需要实现具体代码

## 与 atomic14 项目的对比

| 特性 | atomic14/diy-esp32-epub-reader | 本项目 |
|------|-------------------------------|--------|
| 目标平台 | ESP32-WROVER (520KB RAM + PSRAM) | ESP32-C3 (400KB RAM) |
| 内存需求 | 需要 PSRAM | 无需 PSRAM |
| 解析方式 | 一次性加载 + 完整解析 | 流式解析 + 按需加载 |
| ZIP 库 | miniz-esp32 | 自实现简化版 |
| XML 库 | TinyXML2 | 手动解析（节省空间） |
| 渲染 | 自定义 Renderer | LVGL |
| 存储 | SD 卡 | SD 卡 + Flash 缓存 |

## 使用示例

### 打开 EPUB 文件

```c
#include "epub_parser.h"

epub_reader_t *reader = calloc(1, sizeof(epub_reader_t));
epub_parser_init(reader);

if (epub_parser_open(reader, "/sdcard/books/test.epub")) {
    const epub_metadata_t *metadata = epub_parser_get_metadata(reader);
    printf("Title: %s\nAuthor: %s\nChapters: %d\n",
           metadata->title, metadata->author, metadata->total_chapters);
}
```

### 读取章节

```c
char buffer[8192];
int bytes_read = epub_parser_read_chapter(reader, 0, buffer, sizeof(buffer));
if (bytes_read > 0) {
    // 使用 HTML 解析器提取文本
    epub_html_parser_t *html = epub_html_create(buffer, bytes_read);

    epub_text_block_t block;
    while (epub_html_next_block(html, &block)) {
        printf("%s\n", block.text);
    }

    epub_html_destroy(html);
}
```

### 跳转章节

```c
// 跳转到第 5 章
epub_parser_goto_chapter(reader, 4);

// 下一章
epub_parser_next_chapter(reader);

// 上一章
epub_parser_prev_chapter(reader);
```

### 保存进度

```c
epub_parser_save_position(reader);  // 保存到 NVS
// ... 重启后 ...
epub_parser_load_position(reader);  // 恢复位置
```

## 当前限制

1. **压缩格式**: 目前只支持 store（无压缩），不支持 deflate
2. **HTML 标签**: 只支持基本标签，不支持复杂 CSS
3. **图片**: 提取了图片路径，但渲染需要额外实现
4. **Flash 缓存**: 接口已定义，但实现待完成

## 下一步工作

- [ ] 添加 deflate 解压支持
- [ ] 实现 `epub_cache.c`
- [ ] 与 `reader_screen.c` 集成
- [ ] 添加图片支持
- [ ] 测试编译

## 参考资料

- [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)
- [EPUB 3.2 规范](https://www.w3.org/publishing/epub32/)
- [miniz ZIP 库](https://github.com/richgel999/miniz)
