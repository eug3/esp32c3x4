# EPUB解析器改进报告

## 问题描述
在解析某些EPUB文件（特别是来自Z-Library的书籍）时，EPUB解析器报告"No spine items found"错误，导致文件无法打开。日志显示OPF已成功加载，但无法找到spine项目。

## 根本原因分析

通过诊断脚本和代码审查，我们识别了以下潜在问题：

### 1. **XML属性解析问题** (`extract_attribute` 函数)
- 原始实现使用简单的字符串搜索，没有验证属性是否正确
- 没有正确处理属性名前的空白
- 不支持单引号，只支持双引号
- 可能在属性值包含特殊字符时失败

### 2. **Spine标签结束检测问题**
- 当找不到 `</spine>` 标签时，回退逻辑不够健壮
- 只尝试查找 `<manifest` 或 `</package>`，但某些EPUB可能有不同的结构
- 可能跳过整个spine部分

### 3. **Metadata结束标签检测问题**
- 当找不到 `</metadata>` 标签时，使用文件末尾作为边界
- 这导致metadata和spine之间的边界不清，spine搜索范围不正确

### 4. **命名空间处理不完整**
- 代码检查 `<spine` 和 `<opf:spine`，但没有检查其他变体
- 同样，itemref也可能带有命名空间前缀

## 实现的改进

### 1. **改进 `extract_attribute` 函数**
```c
- 检查属性名之前的字符是否为空白或起始位置
- 检查属性名之后是否正确跟随 '='
- 支持单引号和双引号
- 更健壮的属性值提取
```

### 2. **改进Spine边界检测**
```c
- 添加多个候选边界标签（guide, opf:manifest, opf:spine等）
- 选择最接近spine_start的候选作为边界
- 更好的处理缺失闭合标签的情况
```

### 3. **改进Metadata边界检测**
```c
- 不再使用文件末尾作为默认边界
- 尝试找到下一个主要标签（manifest, spine, guide, </package>）
- 选择最接近的标签作为metadata的结束边界
```

### 4. **改进itemref搜索**
```c
- 同时搜索 <itemref 和 <opf:itemref
- 选择最先出现的itemref
- 支持多种命名空间变体
```

### 5. **增强诊断日志**
```c
- 在spine未找到时，打印更详细的诊断信息
- 显示OPF内容样本用于调试
- 记录已找到的itemref数量
```

## 测试结果

使用诊断脚本测试了4个不同的EPUB文件：

| 文件名 | itemref数量 | 解析结果 |
|-------|-----------|--------|
| 绑架游戏 | 4 | ✓ 成功 |
| oebps.epub | 13 | ✓ 成功 |
| relative_paths.epub | 373 | ✓ 成功 |
| no_oebps.epub | 2 | ✓ 成功 |

所有测试文件都能正确解析。

## 修改的文件

1. **main/ui/epub/epub_xml.c**
   - 改进 `extract_attribute()` 函数
   - 改进 `epub_xml_count_spine_items()` 函数
   - 改进 `epub_xml_parse_spine()` 函数
   - 改进 `epub_xml_parse_metadata()` 函数
   - 添加更详细的诊断日志

2. **main/ui/epub/epub_parser.c**
   - 添加OPF提取失败时的诊断日志

3. **tools/diagnose_epub.py** (新增)
   - 用于诊断EPUB文件结构的Python脚本
   - 能够解析OPF并显示spine和itemref信息

## 后续建议

1. **对有问题的EPUB文件进行测试**
   - 获取原始的"沉默的巡游"文件进行测试
   - 验证新的解析器是否能处理它

2. **添加单元测试**
   - 为xml解析器添加更全面的单元测试
   - 测试各种边界情况和异常OPF格式

3. **性能优化**
   - 考虑缓存spine解析结果
   - 优化大型EPUB文件的内存使用

4. **错误处理**
   - 当无法解析时，提供更有用的错误消息
   - 可能需要自动修复某些常见的OPF格式问题
