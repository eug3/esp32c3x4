# EPUB 解析流程文档

## 目录

- [系统概述](#系统概述)
- [架构设计](#架构设计)
- [核心模块](#核心模块)
- [解析流程](#解析流程)
- [数据结构](#数据结构)
- [API 参考](#api-参考)
- [优化策略](#优化策略)

---

## 系统概述

本 EPUB 解析器专为 ESP32-C3 (400KB RAM) 设计，采用流式解析和三级缓存架构，实现高效的电子书阅读功能。

### 设计目标

- **低内存占用**：流式解析，避免一次性加载整个 EPUB
- **快速响应**：多级缓存机制，减少 SD 卡访问
- **可靠解析**：容错处理，兼容各种 EPUB 格式

---

## 架构设计

### 三级缓存架构

```
┌─────────────────────────────────────────────────────────┐
│                         RAM                              │
│  当前页文本 + 解析状态                                    │
└─────────────────────────────────────────────────────────┘
                          ↕
┌─────────────────────────────────────────────────────────┐
│                   LittleFS (Flash)                      │
│  预缓存窗口 (前后 2-5 章) + OPF 缓存                     │
└─────────────────────────────────────────────────────────┘
                          ↕
┌─────────────────────────────────────────────────────────┐
│                      SD Card                            │
│  完整 EPUB 文件 (ZIP 格式)                               │
└─────────────────────────────────────────────────────────┘
```

### 模块依赖关系

```
┌─────────────────┐
│  epub_parser    │ ← 主解析器
│  (主控制器)      │
└────────┬────────┘
         │
    ┌────┴────┬──────────┬──────────┐
    ↓         ↓          ↓          ↓
┌────────┐ ┌───────┐ ┌────────┐ ┌─────────┐
|epub_zip| |epub_xml| |epub_html| |epub_cache|
│ ZIP解压 │ │ XML解析│ │ HTML提取│ │ 缓存管理 │
└────────┘ └───────┘ └────────┘ └─────────┘
                                        ↑
                                  ┌─────┴─────┐
                                  │epub_precache│
                                  │ 预缓存调度器 │
                                  └─────────────┘
```

---

## 核心模块

### 1. epub_parser (主解析器)

**文件**: [epub_parser.h](main/ui/epub/epub_parser.h), [epub_parser.c](main/ui/epub/epub_parser.c)

**职责**:
- EPUB 文件打开与验证
- 章节信息管理
- 阅读位置保存/加载
- 与其他模块的协调

**核心数据结构**:

```c
// EPUB 章节信息
typedef struct {
    char title[128];         // 章节标题
    char content_file[128];  // 内容文件路径（在 EPUB 内）
    long file_offset;        // 文件偏移
    int chapter_index;       // 章节索引
} epub_chapter_t;

// EPUB 元数据
typedef struct {
    char title[128];         // 书名
    char author[128];        // 作者
    char language[16];       // 语言
    char identifier[64];     // 唯一标识符
    int total_chapters;      // 总章节数
} epub_metadata_t;

// EPUB 阅读器状态
typedef struct {
    char epub_path[256];           // EPUB 文件路径
    FILE *current_file;            // 当前打开的内容文件
    epub_metadata_t metadata;      // 元数据
    char opf_cache_path[256];      // OPF XML 缓存路径
    char opf_base_dir[128];        // OPF 所在目录
    epub_position_t position;      // 当前位置
    bool is_open;                  // 是否已打开
} epub_reader_t;
```

---

### 2. epub_zip (ZIP 解压)

**文件**: [epub_zip.h](main/ui/epub/epub_zip.h), [epub_zip.c](main/ui/epub/epub_zip.c)

**职责**:
- ZIP 文件格式解析
- 使用 miniz 进行流式 deflate 解压
- 按需解压单个文件

**ZIP 解析流程**:

```
1. 定位 End of Central Directory (EOCD)
   └─ 从文件末尾向前搜索 0x06054b50 签名

2. 解析 Central Directory
   └─ 读取所有文件的元信息（文件名、偏移、大小）

3. 按需解压文件
   └─ 跳转到 Local Header
   └─ 跳过文件名/额外字段
   └─ 使用 tinfl 流式解压 deflate 数据
```

**核心数据结构**:

```c
typedef struct {
    char filename[256];        // 文件名（在 ZIP 内的路径）
    uint32_t offset;           // 文件数据在 ZIP 中的偏移
    uint32_t compressed_size;   // 压缩大小
    uint32_t uncompressed_size; // 解压大小
    uint16_t compression_method; // 压缩方法 (0=存储, 8=deflate)
} epub_zip_file_info_t;
```

**Deflate 解压优化**:

- 使用 `TINFL_FLAG_HAS_MORE_INPUT` 正确处理分块输入
- 4KB 输入缓冲区，逐块解压到目标缓冲区
- 精确追踪已读取的压缩字节数

---

### 3. epub_xml (XML 解析)

**文件**: [epub_xml.h](main/ui/epub/epub_xml.h), [epub_xml.cpp](main/ui/epub/epub_xml.cpp)

**职责**:
- 解析 content.opf 文件
- 提取元数据（书名、作者等）
- 解析 spine（章节顺序）和 manifest（文件清单）

**XML 解析策略** (三级回退):

```
┌─────────────────────────────────────────────────────────┐
│ 第一级：原始 XML 直接解析                                 │
│ └─ 使用 TinyXML2 COLLAPSE_WHITESPACE 模式               │
└─────────────────────────────────────────────────────────┘
                          ↓ 失败
┌─────────────────────────────────────────────────────────┐
│ 第二级：清洗后解析                                        │
│ └─ 移除 UTF-8 BOM                                        │
│ └─ 移除控制字符 (除 \t/\n/\r)                            │
│ └─ 移除无效 UTF-8 字节                                   │
│ └─ 移除/截断未终止的 XML 注释                            │
└─────────────────────────────────────────────────────────┘
                          ↓ 失败
┌─────────────────────────────────────────────────────────┐
│ 第三级：截断后解析                                        │
│ └─ 截断到 </spine> 并补齐 </package>                     │
│ └─ 处理尾部垃圾数据                                      │
└─────────────────────────────────────────────────────────┘
```

**OPF 文件结构**:

```xml
<package>
  <metadata>
    <dc:title>书名</dc:title>
    <dc:creator>作者</dc:creator>
    <dc:language>zh</dc:language>
  </metadata>
  <manifest>
    <item id="chapter1" href="OEBPS/chapter1.xhtml"/>
    <item id="chapter2" href="OEBPS/chapter2.xhtml"/>
  </manifest>
  <spine>
    <itemref idref="chapter1"/>
    <itemref idref="chapter2"/>
  </spine>
</package>
```

**核心数据结构**:

```c
typedef struct {
    char title[128];
    char author[128];
    char language[16];
} epub_xml_metadata_t;

typedef struct {
    char idref[64];       // spine 中的 idref
    char href[256];       // 对应 manifest 中的 href
    int index;            // 在 spine 中的索引
} epub_xml_spine_item_t;
```

---

### 4. epub_html (HTML 解析)

**文件**: [epub_html.h](main/ui/epub/epub_html.h), [epub_html.c](main/ui/epub/epub_html.c)

**职责**:
- 提取 HTML/XHTML 中的纯文本内容
- HTML 实体解码 (&amp;, &lt;, &#1234; 等)
- 流式处理，避免一次性加载大文件

**流式解析状态机**:

```c
typedef struct {
    bool in_script;          // 是否在 <script> 中
    bool in_style;           // 是否在 <style> 中
    bool in_tag;             // 是否在标签中
    bool in_comment;         // 是否在注释中
    bool in_entity;          // 是否在 HTML 实体中
    bool wrote_space;        // 上次是否写了空格
    char tag_name[16];       // 当前标签名
    char entity[20];         // 当前实体内容
} epub_html_stream_t;
```

**文本提取规则**:

| 标签 | 处理方式 |
|------|----------|
| `<script>`, `<style>` | 跳过全部内容 |
| `<!-- -->` | 跳过注释 |
| `<br>` | 输出换行符 |
| `<p>`, `<div>`, `<section>` | 输出段落分隔（双换行） |
| `<li>` | 输出 "- " 前缀 |
| `<h1>-<h6>` | 输出段落分隔 |
| `&nbsp;` | 输出空格 |
| `&#...;` | 解码为 Unicode 字符 |

---

### 5. epub_cache (Flash 缓存)

**文件**: [epub_cache.h](main/ui/epub/epub_cache.h), [epub_cache.c](main/ui/epub/epub_cache.c)

**职责**:
- LittleFS 文件缓存管理
- 2MB 缓存空间限制
- 缓存键哈希管理

**缓存目录结构**:

```
/littlefs/epub_cache/
  ├── ec_12345678_0.bin  # 章节 HTML 缓存
  ├── ec_12345678_1.bin  # 章节纯文本缓存
  ├── ec_87654321_0.bin  # 另一个章节
  └── ...
```

**缓存键结构**:

```c
typedef enum {
    EPUB_CACHE_CHAPTER,       // 章节原始 HTML
    EPUB_CACHE_RENDERED_TEXT, // 章节渲染后的纯文本
    EPUB_CACHE_METADATA,
    EPUB_CACHE_IMAGE,
    EPUB_CACHE_INDEX,
} epub_cache_type_t;

typedef struct {
    char epub_path[256];     // EPUB 文件路径
    char content_path[256];  // 内容路径（如 OEBPS/chapter1.xhtml）
    epub_cache_type_t type;  // 缓存类型
} epub_cache_key_t;
```

**哈希计算**:

```c
// FNV-1a 32位哈希
uint32_t hash = fnv1a32(epub_path | "|" | content_path | "|" | type);
// 文件名: /littlefs/epub_cache/ec_<hash>_<type>.bin
```

---

### 6. epub_precache (预缓存管理)

**文件**: [epub_precache.h](main/ui/epub/epub_precache.h), [epub_precache.c](main/ui/epub/epub_precache.c)

**职责**:
- 滑动窗口预缓存
- 自动管理缓存生命周期

**预缓存窗口配置**:

```c
#define PRECACHE_WINDOW_BEFORE 2   // 当前章节之前缓存章节数
#define PRECACHE_WINDOW_AFTER  5   // 当前章节之后缓存章节数
#define PRECACHE_MAX_CHAPTERS  10  // 最大同时缓存章节数
```

**窗口示意**:

```
章节: [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] ...
                        ↑ 当前阅读位置
预缓存窗口: [2] [3] [4] [5] [6] [7] [8] [9]
           (前2章)      (当前)      (后5章)
```

---

## 解析流程

### 完整打开流程

```
epub_parser_open()
│
├─ 1. 验证 EPUB 文件
│   └─ 检查 .epub 扩展名 + ZIP 文件头 (0x50 0x4B)
│
├─ 2. 打开 ZIP 文件
│   └─ epub_zip_open()
│      └─ 解析 Central Directory，构建文件列表
│
├─ 3. 查找并解析 content.opf
│   ├─ 尝试路径: OEBPS/content.opf, OPS/content.opf, content.opf
│   ├─ 解压 OPF 到内存
│   └─ epub_xml_create() → 三级回退解析
│
├─ 4. 提取元数据
│   └─ epub_xml_parse_metadata()
│      └─ 提取书名、作者、语言
│
├─ 5. 解析章节结构
│   ├─ epub_xml_count_spine_items()
│   ├─ epub_xml_parse_spine()
│   └─ 保存章节数量和 spine 信息
│
├─ 6. 缓存 OPF 到 LittleFS
│   └─ 避免下次打开时重新解析
│
└─ 7. 初始化阅读状态
   └─ current_chapter = 0, page_number = 0
```

### 章节读取流程

```
epub_parser_read_chapter_text_at(chapter_index, offset)
│
├─ 1. 检查缓存
│   └─ epub_cache_exists()
│
├─ 2. 如果未缓存，触发预缓存
│   └─ ensure_rendered_text_cache()
│      ├─ ensure_chapter_cached_and_get_key()
│      │  └─ 从 ZIP 解压章节 HTML → LittleFS
│      └─ 流式转换 HTML → 纯文本
│         └─ epub_html_stream_feed()
│
├─ 3. 从缓存读取
│   └─ epub_cache_read() → fread() from LittleFS
│
└─ 4. 返回纯文本内容
```

### 翻页流程

```
用户翻下一页
│
├─ 1. 检测是否需要跳转章节
│   └─ 当前位置 + 页大小 > 章节大小
│
├─ 2. 如果需要，跳转到下一章
│   └─ epub_parser_next_chapter()
│      └─ epub_parser_goto_chapter()
│         └─ epub_precache_update_window()
│
└─ 3. 读取新页内容
   └─ epub_parser_read_chapter_text_at()
```

---

## 数据结构

### 核心状态流转

```
epub_reader_t (主状态)
│
├─ metadata (元数据)
│   ├─ title, author, language
│   └─ total_chapters
│
├─ position (阅读位置)
│   ├─ current_chapter
│   ├─ chapter_position
│   └─ page_number
│
└─ opf_cache_path (OPF 缓存)
   └─ /littlefs/epub_opf_xxxxxxxx.xml
```

### 章节信息获取 (按需)

```
epub_parser_get_chapter_info_impl()
│
├─ 1. 从 LittleFS 读取缓存的 OPF
│
├─ 2. 重新解析 OPF
│   └─ epub_xml_create()
│
├─ 3. 解析 spine
│   └─ epub_xml_parse_spine()
│
├─ 4. 查找 manifest 中的 href
│   └─ epub_xml_find_manifest_item()
│
└─ 5. 构建完整路径
   └─ opf_base_dir + href
```

---

## API 参考

### 基础操作

| 函数 | 说明 |
|------|------|
| `epub_parser_init()` | 初始化解析器 |
| `epub_parser_open(reader, epub_path)` | 打开 EPUB 文件 |
| `epub_parser_close(reader)` | 关闭 EPUB |
| `epub_parser_get_metadata(reader)` | 获取元数据 |

### 章节操作

| 函数 | 说明 |
|------|------|
| `epub_parser_get_chapter_count(reader)` | 获取章节数量 |
| `epub_parser_get_chapter_info(reader, index, out)` | 获取章节信息 |
| `epub_parser_read_chapter_text_at(reader, index, offset, buf, size)` | 按偏移读取章节纯文本 |

### 导航操作

| 函数 | 说明 |
|------|------|
| `epub_parser_goto_chapter(reader, index)` | 跳转到指定章节 |
| `epub_parser_next_chapter(reader)` | 下一章 |
| `epub_parser_prev_chapter(reader)` | 上一章 |

### 位置保存

| 函数 | 说明 |
|------|------|
| `epub_parser_save_position(reader)` | 保存到 NVS |
| `epub_parser_load_position(reader)` | 从 NVS 加载 |

---

## 优化策略

### 1. 内存优化

- **流式解压**: 使用 tinfl 逐块解压，避免分配完整解压缓冲区
- **按需解析**: 章节信息按需获取，不预先分配全部章节数组
- **小块处理**: HTML 解析使用 2KB 输入/输出缓冲区

### 2. 性能优化

- **OPF 缓存**: 解析后的 OPF 保存到 LittleFS，避免重复解析
- **滑动窗口预缓存**: 自动预加载前后章节
- **Flash 缓存**: 已访问章节缓存到 Flash，减少 SD 卡访问

### 3. 容错处理

- **XML 解析回退**: 三级回退策略处理各种格式的 OPF
- **UTF-8 验证**: 移除无效 UTF-8 序列
- **截断恢复**: 处理损坏的 XML 尾部

### 4. Flash 寿命保护

- **缓存上限**: 2MB 缓存限制，达到上限时清空
- **LRU 清理**: 滑动窗口外缓存自动清理
- **批量写入**: 大块写入减少擦写次数

---

## 源文件索引

| 模块 | 头文件 | 实现文件 |
|------|--------|----------|
| Parser | [epub_parser.h](main/ui/epub/epub_parser.h) | [epub_parser.c](main/ui/epub/epub_parser.c) |
| ZIP | [epub_zip.h](main/ui/epub/epub_zip.h) | [epub_zip.c](main/ui/epub/epub_zip.c) |
| XML | [epub_xml.h](main/ui/epub/epub_xml.h) | [epub_xml.cpp](main/ui/epub/epub_xml.cpp) |
| HTML | [epub_html.h](main/ui/epub/epub_html.h) | [epub_html.c](main/ui/epub/epub_html.c) |
| Cache | [epub_cache.h](main/ui/epub/epub_cache.h) | [epub_cache.c](main/ui/epub/epub_cache.c) |
| Precache | [epub_precache.h](main/ui/epub/epub_precache.h) | [epub_precache.c](main/ui/epub/epub_precache.c) |

---

*文档版本: 1.0*
*最后更新: 2026-01-09*
