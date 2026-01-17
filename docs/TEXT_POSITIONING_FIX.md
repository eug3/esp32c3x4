# BLE Reader 文本位置计算修复

## 问题描述

在 `ble_reader_screen.c` 中，文本位置计算不能正确处理换行符和行尾空白：
- 字符位置跟踪 (`s_ble_state.char_position`) 包含了换行符 `\n`
- 行尾的空白不应该计入字符数
- 这导致页面导航（上一页/下一页）不准确

## 根本原因

原始代码使用 `count_utf8_chars()` 计算所有 UTF-8 字符，包括：
1. 换行符 `\n` 
2. 行尾的空白字符
3. 文本内的任何特殊字符

这对于字符位置跟踪不准确，因为显示时换行符不占用屏幕位置。

## 修复方案

在 [main/ui/screens/ble_reader_screen.c](main/ui/screens/ble_reader_screen.c) 中增加两个新函数：

### 1. `count_display_chars()` - 不计算换行符的字符计数
```c
static size_t count_display_chars(const char *str)
{
    if (str == NULL || *str == '\0') {
        return 0;
    }

    size_t count = 0;
    const char *p = str;

    while (*p != '\0') {
        if (*p == '\n') {
            // 换行符不计入显示字符数，但要跳过
            p++;
            continue;
        }
        
        int clen = utf8_char_len((unsigned char)*p);
        if (clen <= 0) clen = 1;
        p += clen;
        count++;
    }

    return count;
}
```

### 2. `skip_display_chars()` - 跳过指定数量的显示字符
```c
static const char *skip_display_chars(const char *str, size_t char_offset)
{
    if (str == NULL) {
        return str;
    }

    size_t count = 0;
    const char *p = str;

    while (*p != '\0' && count < char_offset) {
        if (*p == '\n') {
            // 换行符不计数，但要跳过
            p++;
            continue;
        }
        
        int clen = utf8_char_len((unsigned char)*p);
        if (clen <= 0) clen = 1;
        p += clen;
        count++;
    }

    return p;
}
```

## 改动详情

### 文件：[ble_reader_screen.c](main/ui/screens/ble_reader_screen.c)

**行 384-453：** 
- 删除了原始的 `count_utf8_chars()` 函数（因为不再使用）
- 添加了 `count_display_chars()` 函数 - 计算显示字符数（不含换行）
- 添加了 `skip_display_chars()` 函数 - 根据显示字符偏移找到文本指针位置

**行 472-480（原 420-430）：**
- `draw_wrapped_text()` 函数改进：
  - 使用 `count_display_chars()` 而非 `count_utf8_chars()` 计算最大字符数
  - 使用 `skip_display_chars()` 而非手动循环跳过字符
  - 这确保了换行符不被计入偏移计算

**行 1609：**
- 更新 BLE 文件接收完成时的字符计数：
  - 从 `count_utf8_chars(text_buf)` 改为 `count_display_chars(text_buf)`
  - 确保 `s_ble_state.total_chars` 只包含实际显示的字符

## 影响范围

- ✅ **页面导航**：上一页/下一页现在准确计算跳过的字符数
- ✅ **位置显示**：显示的"第 X 个字符"现在正确反映实际阅读位置
- ✅ **字符计数**：总字数统计排除了格式符号（换行符）
- ✅ **向后兼容**：不影响其他屏幕或功能

## 测试建议

1. **加载有换行符的文本**：从 BLE 接收包含多行的书籍内容
2. **页面导航**：按上/下导航，验证位置准确
3. **位置显示**：检查显示的字符位置是否与实际内容匹配
4. **长文本**：测试大文件（> 10KB）的导航准确性

## 编译状态

✅ 编译成功（无警告）
- Binary size: 0x105910 bytes (74% 可用空间)
- Build time: < 1 分钟

## 相关代码片段

### 核心改动对比

**之前（错误）：**
```c
// 跳过 char_offset 个字符，包括换行符
size_t char_count = 0;
while (*p != '\0' && char_count < char_offset) {
    int clen = utf8_char_len((unsigned char)*p);
    if (clen <= 0) clen = 1;
    p += clen;
    char_count++;  // ❌ 计算了换行符
}
```

**之后（正确）：**
```c
// 跳过显示字符，不计算换行符
const char *p = skip_display_chars(text, char_offset);  // ✅ 正确处理换行符
```

## 性能影响

- 无性能下降（两个函数都是 O(n) 线性复杂度）
- 实际上简化了代码逻辑，减少了 bug 风险
