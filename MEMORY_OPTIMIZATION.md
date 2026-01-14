# ESP32-C3 内存优化方案

## 当前内存使用情况
- **总 DRAM**: 321,296 bytes
- **已使用**: 216,334 bytes (67.33%)
- **剩余堆内存**: ~105KB（BLE 初始化后只剩 ~90KB）

## 主要内存占用源

### 1. 显示缓冲区 (s_framebuffer) - 47KB ⚠️
**位置**: `.bss` 段
**建议**: 
- ✅ 使用 PSRAM（如果硬件支持）
- ✅ 动态分配，用完立即释放
- ✅ 减少缓冲区大小（部分刷新）
- ✅ 使用 4 位灰度而非 8 位

```c
// 修改前
static uint8_t s_framebuffer[480 * 800 / 8];  // 47KB

// 修改后 - 动态分配
uint8_t *framebuffer = heap_caps_malloc(FB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### 2. 中文编码转换表 - 174KB ⚠️⚠️⚠️
**位置**: `oem2uni936` (87KB) + `uni2oem936` (87KB)
**建议**:
- ✅ **移到 Flash** - 使用 `PROGMEM` 或 `DRAM_ATTR` 的反面
- ✅ 按需加载部分编码表
- ✅ 如果不需要完整 GB2312 支持，只保留常用字

```c
// 修改前
static const uint16_t oem2uni936[] = { ... };

// 修改后 - 存储在 Flash
static const uint16_t oem2uni936[] PROGMEM = { ... };
// 或
static const DRAM_ATTR uint16_t oem2uni936[] = { ... };
```

### 3. 字体表 - 30KB
**建议**:
- ✅ 移到 Flash (RODATA)
- ✅ 只保留必需的字体大小
- ✅ 使用压缩字体（如 TrueType）

```c
// 将字体表声明为 const，自动放入 Flash
static const uint8_t Font24_Table[] = { ... };  // 而不是 uint8_t
```

### 4. 阅读器状态 (s_reader_state) - 42KB
**建议**:
- ✅ 检查是否有大数组可以动态分配
- ✅ 减少缓存大小
- ✅ 部分数据移到 Flash

### 5. 启动动画帧 - 20KB
**建议**:
- ✅ 移到 Flash (RODATA)
- ✅ 使用压缩格式（RLE/PNG）
- ✅ 按需解压到小缓冲区

```c
// 修改前
static uint8_t g_boot_anim_frame_0[] = { ... };

// 修改后
static const uint8_t g_boot_anim_frame_0[] PROGMEM = { ... };
```

### 6. BLE 堆栈配置优化

修改 `sdkconfig`:
```kconfig
# 减少 BLE 缓冲区
CONFIG_BT_NIMBLE_ACL_BUF_COUNT=4         # 默认 12
CONFIG_BT_NIMBLE_ACL_BUF_SIZE=255        # 默认 255（已最小）
CONFIG_BT_NIMBLE_HCI_EVT_BUF_SIZE=70     # 默认 70（已最小）
CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT=2  # 默认 30
CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT=3  # 默认 8

# 如果不需要配对功能
CONFIG_BT_NIMBLE_SECURITY_ENABLE=n

# 减少连接数
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1       # 默认 3
```

## 优化优先级

### 🔥 立即优化（节省 ~200KB）
1. ✅ **编码转换表移到 Flash** → 节省 174KB
2. ✅ **字体表移到 Flash** → 节省 30KB

### ⚡ 高优先级（节省 ~60KB）
3. ✅ **Framebuffer 动态分配** → 节省 47KB（使用时占用）
4. ✅ **减少 BLE 配置** → 节省 10-15KB

### 🎯 中等优先级（节省 ~20KB）
5. ✅ **启动动画移到 Flash** → 节省 20KB
6. ✅ **优化 reader_state** → 节省 10-20KB

## 代码修改示例

### 1. 将大数组移到 Flash

在变量声明前添加 `const`，编译器会自动将其放入 `.rodata`（Flash）：

```c
// 文件: main/fonts/font_tables.c

// 修改前（占用 RAM）
uint8_t Font24_Table[] = { 0x00, 0x01, ... };

// 修改后（存储在 Flash）
const uint8_t Font24_Table[] = { 0x00, 0x01, ... };
```

### 2. 动态分配 Framebuffer

```c
// 文件: main/display/display_driver.c

// 修改前
static uint8_t s_framebuffer[FRAMEBUFFER_SIZE];

// 修改后
static uint8_t *s_framebuffer = NULL;

void display_init(void) {
    if (s_framebuffer == NULL) {
        s_framebuffer = heap_caps_malloc(FRAMEBUFFER_SIZE, 
                                         MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (s_framebuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate framebuffer");
            return;
        }
    }
}

void display_deinit(void) {
    if (s_framebuffer != NULL) {
        free(s_framebuffer);
        s_framebuffer = NULL;
    }
}
```

### 3. 使用 IRAM_ATTR 控制代码位置

对于频繁调用的函数，可以放入 IRAM 加速；不常用的放 Flash 节省 RAM：

```c
// 放入 IRAM（快速但占用 RAM）
void IRAM_ATTR critical_function(void) { ... }

// 放入 Flash（节省 RAM 但稍慢）
void ICACHE_FLASH_ATTR non_critical_function(void) { ... }
```

## 预计优化效果

| 优化项 | 节省内存 | 难度 |
|--------|---------|------|
| 编码表移 Flash | 174KB | 低 |
| 字体表移 Flash | 30KB | 低 |
| Framebuffer 动态分配 | 47KB* | 中 |
| 优化 BLE 配置 | 15KB | 低 |
| 启动动画移 Flash | 20KB | 低 |
| **总计** | **~240KB** | - |

*动态分配不直接节省 DRAM 占用，但可以按需使用

## 下一步行动

1. 首先修改所有 const 数组声明（5分钟）
2. 配置 BLE 减少缓冲区（2分钟）  
3. 重新编译测试
4. 如果仍不足，实现 framebuffer 动态分配

## 检查命令

```bash
# 查看内存使用
idf.py size

# 查看详细分段
idf.py size-components

# 查看大符号
nm -S --size-sort build/*.elf | grep -E " [bBdD] " | tail -50
```
