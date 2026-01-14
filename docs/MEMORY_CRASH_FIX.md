# ESP32-C3 内存崩溃问题修复与优化指南

## 1. 崩溃原因分析

### 错误堆栈
```
Guru Meditation Error: Core 0 panic'ed (Load access fault). Exception was unhandled.
MEPC: 0x40059066  strnlen in ROM
vfprintf at vfprintf.c:1143  (fmt=0x0)
```

### 根本原因
**`vfprintf` 收到了 NULL 格式字符串 (`fmt=0x0`)**，这是因为：

1. **screen_manager.c 第 178 行**：当 `screen_name` 为 NULL 时，`ESP_LOGE` 直接传入 NULL
2. **screen->name 为 NULL**：某些屏幕结构体的 `name` 字段未初始化

### 已修复的问题
- [screen_manager.c#L173](../main/ui/core/screen_manager.c#L173) - `screen_manager_show()` 添加 NULL 检查
- [screen_manager.c#L152](../main/ui/core/screen_manager.c#L152) - `screen_manager_unregister()` 安全处理 name
- [screen_manager.c#L199](../main/ui/core/screen_manager.c#L199) - `screen_manager_show_screen()` 安全处理 name

---

## 2. 当前内存使用情况

### 内存总览 (idf.py size)
| 内存区域 | 已使用 | 百分比 | 剩余 | 总计 |
|---------|--------|--------|------|------|
| DRAM | 153,198 bytes | 47.68% | 168,098 bytes | 321,296 bytes |
| .text (DRAM) | 88,354 bytes | 27.5% | - | - |
| .data | 45,820 bytes | 14.26% | - | - |
| .bss | 19,024 bytes | 5.92% | - | - |

### 主要内存占用组件
| 组件 | 大小 | 说明 |
|------|------|------|
| libmain.a | 226 KB | 应用代码（含 34KB .data） |
| libfatfs.a | 175 KB rodata | FATFS 中文编码表（OEM↔Unicode） |
| libbtdm_app.a | 98 KB | 蓝牙协议栈 |
| libbt.a | 74 KB | 蓝牙驱动 |

---

## 3. 高危内存分配点

### 3.1 帧缓冲区 (48 KB)
**位置**: `display_engine.c`
```c
#define FRAMEBUFFER_SIZE ((800 * 480) / 8)  // 48KB
static uint8_t *s_framebuffer = NULL;  // 动态分配 ✓
```
**状态**: ✓ 已优化为动态分配

### 3.2 页面缓冲区 (48 KB)
**位置**: `ble_reader_screen.c`
```c
static uint8_t *s_page_buffer = NULL;
static const size_t PAGE_BUFFER_SIZE = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;  // 48KB
```
**状态**: ✓ 动态分配，但需要确保释放

### 3.3 阅读器状态 (~40 KB)
**位置**: `reader_screen.c`
```c
static struct {
    char file_path[256];
    txt_reader_t txt_reader;
    epub_reader_t epub_reader;
    char *epub_html;      // 4KB 动态
    char *current_text;   // 4KB 动态
    struct txt_cache {
        int32_t src_pos[4001];   // ~16KB
        uint32_t cache_off[4001]; // ~16KB
        // ...
    } txt_cache;
} s_reader_state;
```
**问题**: `txt_cache.src_pos` 和 `cache_off` 占用 32KB 静态内存

### 3.4 FATFS 编码表 (175 KB Flash rodata)
**位置**: `libfatfs.a` 内的 OEM↔Unicode 转换表
**状态**: 已在 Flash，但占用 rodata 空间

---

## 4. 优化建议

### 🔴 立即执行 - 防止崩溃

#### 4.1 检查所有 ESP_LOG 调用中的 %s 参数
```c
// 危险模式
ESP_LOGI(TAG, "Name: %s", ptr->name);  // ptr->name 可能为 NULL

// 安全模式
ESP_LOGI(TAG, "Name: %s", ptr->name ? ptr->name : "(null)");
```

#### 4.2 在格式化函数前添加 NULL 检查
```c
void log_screen_info(screen_t *screen) {
    if (screen == NULL) {
        ESP_LOGE(TAG, "screen is NULL");
        return;
    }
    if (screen->name == NULL) {
        ESP_LOGE(TAG, "screen->name is NULL");
        return;
    }
    ESP_LOGI(TAG, "Screen: %s", screen->name);
}
```

### 🟡 高优先级 - 节省堆内存

#### 4.3 TXT 缓存数组改为动态分配
```c
// 修改前
struct txt_cache {
    int32_t src_pos[TXT_CACHE_CHARS + 1];   // 16KB 静态
    uint32_t cache_off[TXT_CACHE_CHARS + 1]; // 16KB 静态
};

// 修改后
struct txt_cache {
    int32_t *src_pos;   // 动态分配
    uint32_t *cache_off; // 动态分配
};

// 在需要时分配
bool txt_cache_init(void) {
    s_reader_state.txt_cache.src_pos = malloc((TXT_CACHE_CHARS + 1) * sizeof(int32_t));
    s_reader_state.txt_cache.cache_off = malloc((TXT_CACHE_CHARS + 1) * sizeof(uint32_t));
    if (!s_reader_state.txt_cache.src_pos || !s_reader_state.txt_cache.cache_off) {
        txt_cache_deinit();
        return false;
    }
    return true;
}

void txt_cache_deinit(void) {
    free(s_reader_state.txt_cache.src_pos);
    free(s_reader_state.txt_cache.cache_off);
    s_reader_state.txt_cache.src_pos = NULL;
    s_reader_state.txt_cache.cache_off = NULL;
}
```

#### 4.4 降低 BLE 堆栈内存占用
在 `sdkconfig` 中添加：
```kconfig
# 减少 BLE 缓冲区
CONFIG_BT_NIMBLE_ACL_BUF_COUNT=4
CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT=8
CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT=4
```

### 🟢 长期优化

#### 4.5 使用内存池管理
```c
// 创建固定大小的内存池
#define POOL_BLOCK_SIZE 512
#define POOL_BLOCK_COUNT 16
static uint8_t s_mem_pool[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];
static bool s_pool_used[POOL_BLOCK_COUNT];

void* pool_alloc(size_t size) {
    if (size > POOL_BLOCK_SIZE) return NULL;
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) {
        if (!s_pool_used[i]) {
            s_pool_used[i] = true;
            return &s_mem_pool[i * POOL_BLOCK_SIZE];
        }
    }
    return NULL;
}
```

#### 4.6 添加堆内存监控
```c
void print_heap_info(const char *when) {
    ESP_LOGI("HEAP", "%s: Free=%lu, MinFree=%lu, Largest=%lu",
             when,
             esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
```

---

## 5. 调试技巧

### 5.1 启用堆内存追踪
在 `sdkconfig` 中：
```kconfig
CONFIG_HEAP_TRACING_STANDALONE=y
CONFIG_HEAP_TRACING_STACK_DEPTH=4
```

### 5.2 定位内存泄漏
```c
#include "esp_heap_trace.h"

#define NUM_RECORDS 100
static heap_trace_record_t trace_records[NUM_RECORDS];

void start_heap_trace(void) {
    heap_trace_init_standalone(trace_records, NUM_RECORDS);
    heap_trace_start(HEAP_TRACE_LEAKS);
}

void stop_heap_trace(void) {
    heap_trace_stop();
    heap_trace_dump();
}
```

### 5.3 栈溢出检测
在 `sdkconfig` 中：
```kconfig
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

---

## 6. 项目文件结构

```
esp32c3x4/main/
├── main.c                    # 入口，按钮/ADC/SD卡初始化
├── DEV_Config.c/h           # SPI/GPIO 硬件配置
├── EPD_4in26.c/h            # 电子墨水屏驱动
├── GUI_Paint.c/h            # 图形绘制库
├── ImageData.c/h            # 图像数据（目前为空）
├── power_manager.c/h        # 电源管理
├── version.h                # 版本信息
├── lib/
│   ├── pngdec/              # PNG 解码库
│   ├── tjpgd/               # JPEG 解码库
│   └── tinyxml2/            # XML 解析库
└── ui/
    ├── core/
    │   ├── display_engine.c/h    # 显示引擎（帧缓冲管理）
    │   ├── screen_manager.c/h    # 屏幕管理器
    │   ├── input_handler.c/h     # 输入处理
    │   ├── paginated_menu.c/h    # 分页菜单
    │   └── ui_region_manager.c/h # UI 区域管理
    ├── screens/
    │   ├── home_screen.c/h       # 主屏幕
    │   ├── ble_reader_screen.c/h # 蓝牙阅读器
    │   ├── reader_screen.c/h     # 本地阅读器
    │   ├── file_browser_screen.c/h # 文件浏览器
    │   ├── settings_screen.c/h   # 设置
    │   ├── font_select_screen.c/h # 字体选择
    │   ├── wallpaper_screen.c/h  # 壁纸
    │   └── boot_screen.c/h       # 启动屏幕
    ├── ble/
    │   ├── ble_manager.c/h       # BLE 管理
    │   ├── ble_book_protocol.c/h # 书籍协议
    │   └── ble_cache_manager.c/h # BLE 缓存
    ├── epub/
    │   ├── epub_parser.c/h       # EPUB 解析
    │   ├── epub_html.c/h         # HTML 渲染
    │   ├── epub_cache.c/h        # EPUB 缓存
    │   └── epub_zip.c/h          # ZIP 解压
    ├── fonts/
    │   ├── font_cache.c/h        # 字体缓存
    │   ├── font_selector.c/h     # 字体选择器
    │   ├── xt_eink_font.c/h      # 字体渲染
    │   └── chinese_font_impl.c/h # 中文字体实现
    ├── boot/
    │   └── boot_animation.c/h    # 启动动画
    └── wallpaper/
        └── wallpaper_manager.c/h # 壁纸管理
```

---

## 7. 检查清单

- [ ] 所有 `ESP_LOG*` 中的 `%s` 参数都有 NULL 保护
- [ ] 大型静态数组改为动态分配
- [ ] 屏幕切换时释放不再需要的缓冲区
- [ ] 添加堆内存监控日志
- [ ] BLE 堆栈配置已优化
- [ ] 定期运行 `idf.py size` 检查内存使用
