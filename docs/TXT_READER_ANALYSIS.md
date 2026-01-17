# TXT Reader 字符计数和位置跟踪分析

## 系统架构总览

```
txt_reader.c (底层文件读取)
    ↓
txt_cache (缓存管理)
    ↓
reader_screen.c (显示和导航)
```

### 三层体系

| 层次 | 模块 | 职责 |
|------|------|------|
| **第1层：底层读取** | `txt_reader.c` | 逐字符读取文件，按编码处理（UTF-8/GB18030） |
| **第2层：缓存** | `reader_screen.c::txt_cache_*` | 预读4000字符到内存缓冲，建立字符→文件位置映射 |
| **第3层：显示** | `reader_screen.c::txt_cache_format_current_page()` | 根据屏幕宽度自动换行，格式化输出页面 |

---

## 第1层：txt_reader.c 底层逻辑

### 关键数据结构

```c
typedef struct {
    long file_position;      // 当前文件位置（字节）
    int page_number;         // 当前页码
    int total_pages;         // 总页数估算
    long file_size;          // 文件大小
} txt_position_t;

typedef struct {
    FILE *file;              // 文件句柄
    txt_encoding_t encoding; // 文件编码
    txt_position_t position; // 当前位置
    // ...
} txt_reader_t;
```

### 核心函数：`txt_reader_read_page()`

**功能**：逐字符读取，直到达到 `chars_per_page` 个字符为止

**关键逻辑**：

```c
int txt_reader_read_page(txt_reader_t *reader, char *text_buffer, 
                          size_t buffer_size, int chars_per_page) {
    int chars_count = 0;  // ⭐️ 字符计数变量
    
    while (chars_count < chars_per_page && ...) {
        // 1. 跳过 CR 字符
        if (c == '\r') {
            continue;  // ❌ 不计数，不输出
        }
        
        // 2. 处理 LF（换行符）
        if (c == '\n') {
            output[written++] = '\n';
            chars_count++;  // ✅ 换行符也计数！
            continue;
        }
        
        // 3. 处理普通字符
        if (c < 0x80) {
            // ASCII
            output[written++] = c;
            chars_count++;  // ✅ 计数
        } else {
            // UTF-8/GB18030 多字节字符
            // 读完整个字符（2-4字节）
            chars_count++;  // ✅ 计数为1个字符
        }
    }
    
    return chars_count;  // 返回实际读到的字符数
}
```

**问题 #1**：`chars_count` **包含了换行符**！
- 每个 `\n` 被计为1个字符
- 这对 `char_position` 位置跟踪是错的

**问题 #2**：`file_position` 跟踪的是**字节偏移**，不是字符位置

---

## 第2层：txt_cache 缓存体系

### 核心数据结构

```c
typedef struct {
    FILE *fp;                              // 缓存文件指针
    
    // 两个关键映射数组（每个4000项）
    int32_t src_pos[TXT_CACHE_CHARS + 1];  // src_pos[i] = 第i个字符在文件中的起始字节位置
    uint32_t cache_off[TXT_CACHE_CHARS + 1]; // cache_off[i] = 第i个字符在缓存文件中的字节偏移
    
    uint16_t cached_chars;      // 缓存中实际有多少个字符
    uint16_t cursor;            // 当前读取游标（字符索引）
    int last_page_consumed_chars; // 上一页消耗的字符数
} txt_cache_t;
```

### 函数：`txt_cache_build_at()`

**功能**：从文件指定字节位置开始，预读4000个字符到缓存

```c
static bool txt_cache_build_at(int32_t start_src_pos, uint16_t cursor_reset) {
    // 1. 定位到文件的起始位置（字节）
    txt_reader_seek(&s_reader_state.txt_reader, start_src_pos);
    
    // 2. 逐个读取字符（每次调用 read_next_char_utf8()）
    for (uint16_t i = 0; i < TXT_CACHE_CHARS; i++) {
        char utf8[4];
        uint8_t ulen;
        int32_t src_start = 0;
        
        int ok = read_next_char_utf8(&s_reader_state.txt_reader, 
                                     utf8, &ulen, &src_start);
        if (ok == 0) break;
        
        // ⭐️ 关键映射：记录第i个字符的元数据
        s_reader_state.txt_cache.src_pos[i] = src_start;  // 文件字节位置
        s_reader_state.txt_cache.cache_off[i] = off;      // 缓存字节偏移
        
        // 写到缓存文件
        fwrite(utf8, 1, ulen, fp);
        off += ulen;
        
        s_reader_state.txt_cache.cached_chars++;
    }
    
    // 3. 在哨兵位置记录"下一字符"的位置
    s_reader_state.txt_cache.src_pos[cached_chars] = 
        s_reader_state.txt_reader.position.file_position;
    
    // 4. 设置游标
    s_reader_state.txt_cache.cursor = cursor_reset;
}
```

**关键特性**：
- ✅ 建立了**字符序号** → **文件字节位置** 的映射
- ✅ 这允许快速跳转到任意字符位置
- ✅ `src_pos[cursor]` 可以用来保存进度（文件位置）

**问题 #3**：`read_next_char_utf8()` 会读取 **`\n` 作为字符**，同样被映射记录

---

## 第3层：显示和自动换行

### 函数：`txt_cache_format_current_page()`

**功能**：根据屏幕宽度自动换行，格式化当前屏幕要显示的内容

**关键参数**：
- `target_lines`：目标行数（由 `calculate_chars_per_page()` 转换而来）
- `out_consumed_chars`：输出参数，返回本页消耗的字符数

```c
static int txt_cache_format_current_page(char *out, size_t out_size, 
                                         int target_lines, 
                                         int *out_consumed_chars) {
    // 1. 从当前游标开始
    uint16_t cur = s_reader_state.txt_cache.cursor;
    
    // 2. 逐个读取字符，根据宽度自动换行
    int consumed = 0;
    int lines = 0;
    int line_w = 0;  // 当前行宽度
    
    while (lines < target_lines) {
        uint16_t idx = cur + consumed;
        char ch[5];
        
        // 从缓存读取字符
        uint32_t len = cache_off[idx+1] - cache_off[idx];
        fread(ch, 1, len, fp);
        
        // 特殊处理：源文件中的换行符
        if (ch[0] == '\n') {
            out[written++] = '\n';
            lines++;          // ✅ 源文件换行算一行
            line_w = 0;
            consumed++;
            continue;
        }
        
        // 计算字符宽度
        int cw = txt_cache_fast_char_width(ch, len, ascii_w, cjk_w);
        
        // 检查是否需要自动换行
        if (line_w + cw > max_width && line_w > 0) {
            out[written++] = '\n';
            lines++;          // ✅ 自动换行算一行
            line_w = 0;
            // ⚠️  不消耗当前字符，让它在下一行显示
            fseek(fp, -(long)len, SEEK_CUR);
            continue;
        }
        
        // 正常写入字符
        memcpy(out + written, ch, len);
        written += len;
        line_w += cw;
        consumed++;
    }
    
    *out_consumed_chars = consumed;
    return (int)written;
}
```

**关键点**：
- 输出的 `consumed` = **字符数**（包括 `\n`）
- 这用于计算下一页的游标位置：`cursor += consumed`

---

## 关键问题分析

### 问题汇总表

| # | 问题 | 位置 | 影响 | 严重性 |
|---|------|------|------|--------|
| 1 | `chars_count` 包含 `\n` | txt_reader.c:346 | 页面计数不准 | 🟡 中等 |
| 2 | `file_position` 是字节，不是字符位置 | txt_reader.c 全局 | 恢复位置错误 | 🔴 严重 |
| 3 | BLE reader 使用 `count_utf8_chars()` | ble_reader_screen.c | 导航不准 | 🔴 严重 |
| 4 | txt_cache 预读的 `consumed` 包含 `\n` | reader_screen.c:628 | 游标漂移 | 🟡 中等 |

### 具体案例

假设文件内容为：
```
Hello
World
End
```

**UTF-8 编码字节**：
```
H e l l o \n W o r l d \n E n d
0 1 2 3 4  5 6 7 8 9 10 11 12 13 14
```

**txt_reader_read_page() 行为（chars_per_page=7）**：
```
chars_count=0: Read 'H' → chars_count=1
chars_count=1: Read 'e' → chars_count=2
chars_count=2: Read 'l' → chars_count=3
chars_count=3: Read 'l' → chars_count=4
chars_count=4: Read 'o' → chars_count=5
chars_count=5: Read '\n' → chars_count=6 ⚠️  换行符被计数！
chars_count=6: Read 'W' → chars_count=7 ✅ 停止

返回: chars_count=7, text_buffer="Hello\nW"
file_position=7 (指向第7个字节，即'o'后的'\n')
```

**问题**：
- 用户期望"7个字符"是 7 个**可见字符**（不含 `\n`）
- 实际上得到了 5 个可见字符 + 1 个换行符 + 1 个字符
- 下次读取从字节7开始，会跳过'W'！

---

## 与 BLE Reader 的不同

### reader_screen.c（TXT 模式）

✅ **使用文件位置（字节）**：
- `txt_reader_seek()` 接受字节偏移
- `txt_cache_build_at()` 从字节位置开始
- `src_pos[]` 记录的是字节位置

❌ **但计数时包含 `\n`**：
- `chars_count` 包括换行符
- `consumed` 包括换行符
- 导致游标位置可能不准

### ble_reader_screen.c（BLE 模式）

❌ **使用字符位置（不准确）**：
- `s_ble_state.char_position` 是字符序号
- 原始代码用 `count_utf8_chars()` 计数，包括 `\n`
- ✅ 已修复为 `count_display_chars()`（排除 `\n`）

---

## 推荐的修复方案

### 方案 A：改进 txt_reader（最小改动）

在 `txt_reader.c` 中增加新函数：
```c
// 计算文本内容中的"显示字符数"（不含 \n）
static size_t count_display_chars_in_text(const char *text, size_t len)
{
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        if (text[i] == '\n') {
            i++;
            continue;  // 不计数
        }
        
        int clen = utf8_char_len((unsigned char)text[i]);
        if (clen <= 0) clen = 1;
        i += clen;
        count++;
    }
    return count;
}
```

修改 `txt_reader_read_page()`：
```c
// 改为返回"显示字符数"（不含 \n）
// 或者添加输出参数 int *out_display_chars
```

### 方案 B：统一 txt_cache 和 BLE reader

让两个模块都：
1. **字符计数不包括 `\n`**
2. **使用 `skip_display_chars()` 跳过字符**（已在 ble_reader_screen.c 实现）

---

## 当前代码流程

```
用户按"下一页"
    ↓
reader_screen.c::next_page()
    ↓
s_reader_state.txt_cache.cursor += last_page_consumed_chars
    ↓  (consumed包括\n，导致可能越界或不准)
    ↓
txt_cache_build_at( src_pos[new_cursor] )
    ↓
返回新一页内容
```

**隐患**：如果 `consumed` 计数不准，`new_cursor` 会指向错误位置

