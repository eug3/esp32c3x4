# EPUB 预缓存集成检查清单

## 编译前检查

- [x] ✅ 已创建 `main/ui/epub/epub_precache.h`
- [x] ✅ 已创建 `main/ui/epub/epub_precache.c`
- [x] ✅ 已修改 `main/ui/epub/epub_parser.c`（添加头文件和触发调用）
- [x] ✅ 已更新 `main/CMakeLists.txt`（添加 epub_precache.c）
- [x] ✅ 已创建文档和示例

## 编译检查

### 1. 清理并重新编译

```bash
cd /Users/beijihu/Github/esp32c3x4/c3x4_main_control
idf.py fullclean
idf.py build
```

### 2. 检查编译结果

预期：
- ✅ 编译成功（无错误）
- ⚠️ 可能有未使用头文件的警告（不影响功能）

### 3. 检查二进制大小

```bash
idf.py size
```

预期新增：
- Text 段增加：~2-3 KB（新代码）
- 无额外 RAM 占用（只在调用时动态分配）

## 代码检查

### epub_precache.h

- [x] 定义了所有公共 API
- [x] 配置宏正确（PRECACHE_WINDOW_BEFORE, AFTER, MAX_CHAPTERS）
- [x] 包含保护宏
- [x] 添加了详细注释

### epub_precache.c

- [x] 实现了所有声明的函数
- [x] 包含错误检查和日志
- [x] 使用现有的 epub_cache 和 epub_zip API
- [x] 内存管理正确（malloc/free 配对）

### epub_parser.c

- [x] 添加了 `#include "epub_precache.h"`
- [x] 在 `epub_parser_goto_chapter()` 中调用 `epub_precache_update_window()`
- [x] 调用位置正确（在设置位置之后）

### CMakeLists.txt

- [x] 在 EPUB 模块源文件列表中添加了 `"ui/epub/epub_precache.c"`
- [x] 位置正确（与其他 EPUB 文件在一起）

## 功能测试清单

### 基础功能

```c
// 在 main.c 或适当位置添加初始化
epub_precache_init();
```

- [ ] 打开一本 EPUB 书籍
- [ ] 跳转到第 5 章
- [ ] 查看日志，确认预缓存窗口 [3, 10]
- [ ] 检查 LittleFS 中是否有缓存文件

### 翻页测试

- [ ] 向后翻页（next_chapter）
- [ ] 验证窗口自动更新
- [ ] 检查旧章节缓存被清理
- [ ] 向前翻页（prev_chapter）
- [ ] 验证窗口正确调整

### 性能测试

- [ ] 首次打开章节（需要解压，较慢）
- [ ] 翻到已缓存章节（应该很快，<100ms）
- [ ] 连续快速翻页 10 次
- [ ] 检查响应速度是否一致

### 边界测试

- [ ] 跳转到第 1 章（窗口向前边界）
- [ ] 跳转到最后一章（窗口向后边界）
- [ ] 跳转到中间章节
- [ ] 快速跳转到远处章节

### 资源管理

- [ ] 检查 Flash 使用量（应在 2MB 以内）
- [ ] 长时间阅读（观察是否内存泄漏）
- [ ] 切换书籍后检查缓存（应该分离）

## 日志验证

### 预期日志输出

启动时：
```
I (xxx) EPUB_PRECACHE: Initializing precache manager (window: -2/+5 chapters)
I (xxx) EPUB_CACHE: Cache ready: used=0 total=2097152
```

跳转章节时：
```
I (xxx) EPUB_PRECACHE: Updating precache window: current=5, total=20
I (xxx) EPUB_PRECACHE: Precache window: [3, 10]
I (xxx) EPUB_PRECACHE: Precaching chapter 3: chapter03.xhtml (25600 bytes)
I (xxx) EPUB_PRECACHE: ✓ Chapter 3 cached: 25600 bytes
I (xxx) EPUB_PRECACHE: Chapter 4 already cached: chapter04.xhtml
...
I (xxx) EPUB_PRECACHE: Precache window updated: 8 cached, 0 failed
I (xxx) EPUB_PRECACHE: Cleaned up 2 chapters outside window
```

### 错误日志检查

如果看到这些日志，需要检查：

```
E (xxx) EPUB_PRECACHE: Failed to open EPUB: ...
```
→ 检查文件路径和权限

```
E (xxx) EPUB_PRECACHE: Failed to allocate ... bytes
```
→ 检查可用内存，可能章节太大

```
W (xxx) EPUB_PRECACHE: Reached max precache limit (10 chapters)
```
→ 正常，达到配置上限

## 集成到现有代码

### main.c 修改

在 `app_main()` 函数中添加：

```c
#include "epub_precache.h"

void app_main(void)
{
    // ... 现有初始化代码 ...
    
    // 初始化 LittleFS（应该已存在）
    // ...
    
    // 初始化 EPUB 缓存（应该已存在）
    epub_cache_init();
    
    // 【新增】初始化预缓存
    epub_precache_init();
    
    // ... 其他代码 ...
}
```

### 无需修改的地方

- ❌ 不需要修改 `reader_screen_simple.c`
- ❌ 不需要修改 `epub_html.c`
- ❌ 不需要修改 `epub_zip.c`
- ❌ 不需要修改 `epub_cache.c`

预缓存功能会通过 `epub_parser.c` 自动触发。

## 配置调优

### 默认配置（推荐）

```c
#define PRECACHE_WINDOW_BEFORE 2
#define PRECACHE_WINDOW_AFTER  5
#define PRECACHE_MAX_CHAPTERS  10
```

适合：一般阅读场景

### 快速阅读配置

```c
#define PRECACHE_WINDOW_BEFORE 1
#define PRECACHE_WINDOW_AFTER  7
#define PRECACHE_MAX_CHAPTERS  10
```

适合：快速向前翻页

### 省空间配置

```c
#define PRECACHE_WINDOW_BEFORE 1
#define PRECACHE_WINDOW_AFTER  2
#define PRECACHE_MAX_CHAPTERS  5
```

适合：Flash 空间受限

## 故障排除

### 问题：编译错误 "undefined reference to epub_precache_xxx"

解决：
- 检查 `CMakeLists.txt` 是否包含 `"ui/epub/epub_precache.c"`
- 运行 `idf.py fullclean && idf.py build`

### 问题：编译警告 "unused include"

解决：
- 这些是代码风格警告，不影响功能
- 可以忽略或根据提示删除未使用的头文件

### 问题：没有看到预缓存日志

解决：
- 检查是否调用了 `epub_precache_init()`
- 检查日志级别设置（应该能看到 INFO 级别）
- 使用 `idf.py monitor` 查看完整日志

### 问题：翻页仍然很慢

解决：
- 检查窗口配置是否合理
- 查看日志确认章节是否真的被缓存
- 首次访问章节需要解压，会比较慢
- 确认 LittleFS 正常工作

### 问题：Flash 空间不足

解决：
- 减小 `PRECACHE_MAX_CHAPTERS`
- 减小 `PRECACHE_WINDOW_AFTER`
- 检查 `partitions.csv` 中 littlefs 分区大小

## 性能基准

### 翻页时间测量

使用以下代码测试：

```c
#include "esp_timer.h"

int64_t start = esp_timer_get_time();
epub_parser_goto_chapter(&reader, 7);
int64_t elapsed = esp_timer_get_time() - start;
ESP_LOGI(TAG, "goto_chapter took: %lld ms", elapsed / 1000);
```

预期结果：
- 首次访问（需要解压）：500-1000ms
- 缓存命中：30-100ms
- 差异明显：>5x 提升

## 最终验收

全部通过即可发布：

- [ ] ✅ 编译无错误
- [ ] ✅ 功能测试通过
- [ ] ✅ 性能提升明显（>5x）
- [ ] ✅ 无内存泄漏
- [ ] ✅ 日志输出正常
- [ ] ✅ 文档完整

## 文档清单

已创建的文档：

- [x] `EPUB_PRECACHE_DESIGN.md` - 详细设计文档
- [x] `EPUB_PRECACHE_IMPLEMENTATION.md` - 实现报告
- [x] `EPUB_PRECACHE_QUICKREF.md` - 快速参考
- [x] `CHANGELOG.md` - 更新日志
- [x] `main/ui/epub/epub_precache_example.c` - 代码示例
- [x] 本清单 - 集成检查

---

**检查完成日期**: __________  
**检查人**: __________  
**状态**: ⬜ 待检查 / ⬜ 进行中 / ⬜ 已完成

## 快速命令

```bash
# 清理并编译
idf.py fullclean && idf.py build

# 编译并烧录
idf.py build flash

# 监控日志
idf.py monitor

# 查看大小
idf.py size

# 完整流程
idf.py fullclean build flash monitor
```

---

祝集成顺利！🎉
