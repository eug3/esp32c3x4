/**
 * @file ble_reader_screen.c
 * @brief 蓝牙读书屏幕实现 - TXT 文本显示，支持滑动窗口缓存
 * 
 * X4IM v2 协议 (32 字节头部):
 *   magic (4字节) = "X4IM" (0x58 0x34 0x49 0x4D)
 *   version (1字节) = 0x02 (v2)
 *   type (1字节) = 文件类型或命令
 *   flags (2字节, 小端序) = 标志位
 *   payload_size (4字节, 小端序) = 数据大小
 *   sequence (2字节, 小端序) = 序列号/页码
 *   reserved (2字节) = 保留字段
 *   filename (16字节) = 文件名（UTF-8，以\0结尾）
 * 
 * 三槽队列协议:
 *   - 文件名格式: page_{logicalIndex}
 *   - 槽位映射: slot_id = abs(logical_index) % 3
 *   - 窗口范围: [current-1, current, current+1]
 */

#include "ble_reader_screen.h"
#include "ble_manager.h"
#include "ble_book_protocol.h"
#include "ble_cache_manager.h"
#include "display_engine.h"
#include "fonts.h"
#include "xt_eink_font_impl.h"
#include "../fs_utils.h"     // 共享文件系统工具
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "ff.h"           // FATFS for SD card operations
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

static const char *TAG = "BLE_READER";

#define BLE_TEXT_MAX_BYTES 4096  // 减小缓冲区以节省内存 (原8192太大导致内存碎片)
 

// 内容区域布局常量（显示、翻页、消耗计算统一使用）
#define BLE_CONTENT_Y_START 30      // 内容区域起始 Y（页码信息下方）
#define BLE_CONTENT_BOTTOM_MARGIN 10 // 底部留白
#define BLE_CONTENT_X_MARGIN 10      // 左右边距

// 固定 3 槽滑动窗口（-1, 0, +1）
#define BLE_SLOT_DIR   "/littlefs/ble_slots"
#define BLE_SLOT_COUNT 3

typedef struct {
    int32_t logical_index;   // 对应的逻辑页索引，可为负
    bool ready;              // 文件是否已写完可读
    char path[64];           // 槽位文件路径（slot0/1/2）
} ble_slot_entry_t;

static ble_slot_entry_t s_slots[BLE_SLOT_COUNT];

// ========== X4IM v2 命令定义 ==========
// 魔术头：所有命令必须以此开头，避免与 UTF-8 文本冲突
#define X4IM_MAGIC_HEADER_0        0xA5
#define X4IM_MAGIC_HEADER_1        0x5A

#define X4IM_CMD_SET_MODE          0x8C    // 设置工作模式
#define X4IM_CMD_GET_MODE          0x8D    // 查询当前模式
#define X4IM_CMD_FILE_NOTIFY       0x8B    // 文件通知
#define X4IM_CMD_LIST_FILES        0x8E    // 列表 SD 卡文件
#define X4IM_CMD_DELETE_FILE       0x8F    // 删除 SD 卡文件/目录
#define X4IM_CMD_RENAME_FILE       0x90    // 重命名 SD 卡文件/目录
#define X4IM_CMD_CLEAR_BOOKS       0x91    // 清空书籍目录 (/sdcard/books/)
#define X4IM_CMD_CREATE_DIR        0x92    // 创建目录
#define X4IM_CMD_GET_STORAGE_INFO  0x93    // 获取存储信息
#define X4IM_CMD_READ_FILE         0x94    // 读取文件（下载）
#define X4IM_CMD_FILE_DATA         0x95    // 文件数据块（ESP32→Client）
#define X4IM_CMD_POSITION_SNAPSHOT 0x97    // 章节快照（ESP32→Client，哈希）
#define X4IM_CMD_SET_BOOK_CHAPTER  0x98    // Client→ESP32 设置当前书/章哈希

// 蓝牙读书屏幕实例（导出以供屏幕管理器注册）
screen_t g_ble_reader_screen = {0};

// 蓝牙读书屏幕状态
typedef struct {
    ble_reader_state_t state;           // 当前状态
    ble_work_mode_t work_mode;          // 工作模式（阅读/传输）
    uint32_t current_book_hash;         // 当前书籍哈希（来源于 URL hash）
    uint32_t current_chapter_hash;      // 当前章节哈希（来源于 URL hash）
    uint16_t current_book_id;           // 兼容旧逻辑的书籍ID（低16位）
    uint16_t current_page;              // 当前显示的页码/章节索引（兼容字段）
    uint16_t total_pages;               // 总页数
    uint8_t connected_device[6];        // 已连接的设备地址
    bool device_connected;              // 设备是否已连接
    
    // 状态标志
    bool page_loaded;                   // 当前页面是否已加载
    
    // 预加载状态
    bool preload_requested;             // 是否已请求预加载
    uint16_t preload_start_page;        // 预加载的起始页

    // ========== 翻页防抖和同步 ==========
    bool initialization_complete;       // 初始化（首次收到三页）是否完成
    bool showing_confirm_prompt;        // 是否显示"点击确认"提示
    
    // 缓存窗口（三页：当前、前、后）
    uint16_t cached_pages[3];           // 缓存的页码：[prev, current, next]

    // ========== 文本阅读位置 ==========
    size_t char_position;                // 当前 TXT 文件内的字符位置
    size_t total_chars;                  // 当前 TXT 文件的总字符数

    // ===== 翻页游标与历史 =====
    size_t last_page_consumed;           // 最近一次绘制实际消耗的显示字符数（不含换行，忽略换行后的前导空白）
    size_t history_char_pos[16];         // 历史页起始游标（字符位置）
    uint8_t history_len;                 // 历史深度

    // ========== 传输模式状态 ==========
    uint32_t transfer_bytes_received;   // 已接收字节数
    uint32_t transfer_bytes_total;      // 总字节数
    char transfer_filename[64];         // 当前传输的文件名
    uint16_t transfer_file_count;       // 已传输文件数
} ble_reader_state_internal_t;

static ble_reader_state_internal_t s_ble_state = {
    .state = BLE_READER_STATE_IDLE,
    .work_mode = BLE_MODE_READING,      // 默认阅读模式
    .current_book_hash = 0,
    .current_chapter_hash = 0,
    .current_book_id = 0,
    .current_page = 0,
    .total_pages = 0,
    .device_connected = false,
    .page_loaded = false,
    .preload_requested = false,
    .preload_start_page = 0,
    .char_position = 0,
    .total_chars = 0,
    .initialization_complete = false,
    .showing_confirm_prompt = false,
    .cached_pages = {0, 0, 0},
    .transfer_bytes_received = 0,
    .transfer_bytes_total = 0,
    .transfer_filename = {0},
    .transfer_file_count = 0,
};

// 屏幕上下文
static screen_context_t *s_context = NULL;

// 页面缓冲区（用于 on_draw 显示）- 静态分配避免堆碎片
static uint8_t *s_page_buffer = NULL;
static const size_t PAGE_BUFFER_SIZE = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
// 记录当前缓冲区中加载的是哪一页的位图，减少重复文件读取
static uint16_t s_buffered_page_id = 0xFFFF; // 0xFFFF 表示无效/未加载

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void __attribute__((unused)) on_show(screen_t *screen);
static void __attribute__((unused)) on_hide(screen_t *screen);
static void __attribute__((unused)) on_draw(screen_t *screen);
static void __attribute__((unused)) on_event(screen_t *screen, button_t btn, button_event_t event);

// 小窗口槽位管理
static bool ensure_slot_dir(void);
static void slot_init(void);
static int slot_id_from_index(int32_t idx);
static void slot_prepare_window(int32_t center_index);
static bool slot_get_path_if_ready(int32_t logical_index, char *out_path, size_t out_size);
static void slot_mark_ready(int slot_id, int32_t logical_index);
static bool parse_index_from_name(const char *name, int32_t *out_index);

// 蓝牙回调函数
static void ble_connect_callback(bool connected);
static void ble_data_received_callback(const uint8_t *data, uint16_t length);

// 协议回调
static bool __attribute__((unused)) on_page_ready(uint16_t book_id, uint16_t page_num);
static void __attribute__((unused)) on_preload_needed(uint16_t book_id, uint16_t start_page, uint8_t page_count);

// 双模式界面绘制
static void __attribute__((unused)) draw_transfer_mode_screen(void);
static void __attribute__((unused)) draw_reading_mode_screen(bool clear_content);

// 双模式按键处理
static void __attribute__((unused)) handle_transfer_mode_button(screen_t *screen, button_t btn);
static void __attribute__((unused)) handle_reading_mode_button(screen_t *screen, button_t btn);

// 翻页防抖和同步
static void __attribute__((unused)) send_page_sync_notification(uint16_t page_num);
static void __attribute__((unused)) send_position_snapshot(void);
static void __attribute__((unused)) update_cached_window(uint16_t current_page);
static void __attribute__((unused)) cleanup_old_pages(uint16_t current_page);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 初始化页面缓冲区（静态分配）
 */
static bool init_page_buffer(void)
{
    if (s_page_buffer == NULL) {
        // 记录当前可用内存情况，便于定位碎片问题
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Heap free=%zu, largest=%zu before page buffer alloc", free_internal, largest_internal);

        // 恢复原策略：ESP32-C3没有SPIRAM，会自动fallback到malloc
        s_page_buffer = (uint8_t *)heap_caps_malloc(PAGE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (s_page_buffer == NULL) {
            // 尝试从内部RAM分配
            s_page_buffer = (uint8_t *)malloc(PAGE_BUFFER_SIZE);
        }
        if (s_page_buffer != NULL) {
            ESP_LOGI(TAG, "Page buffer allocated at %p (%zu bytes)",
                     s_page_buffer, PAGE_BUFFER_SIZE);
            size_t free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            ESP_LOGI(TAG, "Heap free=%zu, largest=%zu after page buffer alloc", free_after, largest_after);
        } else {
            ESP_LOGE(TAG, "Failed to allocate page buffer (%zu bytes)", PAGE_BUFFER_SIZE);
            return false;
        }
    }
    return true;
}

/**
 * @brief 释放页面缓冲区
 */
static void deinit_page_buffer(void)
{
    if (s_page_buffer != NULL) {
        free(s_page_buffer);
        s_page_buffer = NULL;
        s_buffered_page_id = 0xFFFF; // 重置缓冲区状态
        ESP_LOGI(TAG, "Page buffer deallocated");
    }
}

// ========== 三槽滑动窗口工具 ==========

static bool ensure_slot_dir(void)
{
    struct stat st;
    if (stat(BLE_SLOT_DIR, &st) != 0) {
        if (mkdir(BLE_SLOT_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create slot dir: %s", BLE_SLOT_DIR);
            return false;
        }
    }
    return true;
}

static int slot_id_from_index(int32_t idx)
{
    int m = idx % BLE_SLOT_COUNT;
    if (m < 0) {
        m += BLE_SLOT_COUNT;
    }
    return m;
}

static void slot_init(void)
{
    if (!ensure_slot_dir()) {
        return;
    }

    for (int i = 0; i < BLE_SLOT_COUNT; i++) {
        s_slots[i].logical_index = INT32_MIN;
        s_slots[i].ready = false;
        // 将书籍哈希带入文件名，避免不同书籍/章节混用槽文件
        snprintf(s_slots[i].path, sizeof(s_slots[i].path), "%s/0x%08" PRIx32 "_slot%d.txt",
                 BLE_SLOT_DIR, s_ble_state.current_book_hash, i);
        unlink(s_slots[i].path); // 只保留干净的三个槽位文件
    }
}

static void slot_prepare_window(int32_t center_index)
{
    if (!ensure_slot_dir()) {
        return;
    }

    int32_t targets[BLE_SLOT_COUNT] = {center_index - 1, center_index, center_index + 1};
    for (int i = 0; i < BLE_SLOT_COUNT; i++) {
        int slot_id = slot_id_from_index(targets[i]);
        ble_slot_entry_t *slot = &s_slots[slot_id];

        if (slot->logical_index != targets[i]) {
            // 该槽位要被复用，删除旧文件并标记 pending
            unlink(slot->path);
            slot->logical_index = targets[i];
            slot->ready = false;
        }
    }
}

static bool slot_get_path_if_ready(int32_t logical_index, char *out_path, size_t out_size)
{
    int slot_id = slot_id_from_index(logical_index);
    ble_slot_entry_t *slot = &s_slots[slot_id];

    if (slot->logical_index != logical_index || !slot->ready) {
        return false;
    }

    if (out_path != NULL && out_size > 0) {
        snprintf(out_path, out_size, "%s", slot->path);
    }
    return true;
}

static void slot_mark_ready(int slot_id, int32_t logical_index)
{
    if (slot_id < 0 || slot_id >= BLE_SLOT_COUNT) {
        return;
    }

    s_slots[slot_id].logical_index = logical_index;
    s_slots[slot_id].ready = true;
}

static bool parse_index_from_name(const char *name, int32_t *out_index)
{
    if (name == NULL || out_index == NULL) {
        return false;
    }

    // 在文件名中找到第一个数字或负号开始的位置
    const char *p = name;
    while (*p != '\0' && !((*p >= '0' && *p <= '9') || *p == '-')) {
        p++;
    }
    if (*p == '\0') {
        return false;
    }

    char *end_ptr = NULL;
    long v = strtol(p, &end_ptr, 10);
    if (end_ptr == p) {
        return false;
    }

    *out_index = (int32_t)v;
    return true;
}

/**
 * @brief 标记页面已加载（实际数据在littlefs中，不预加载）
 */
static bool load_current_page(void)
{
    char slot_path[64];

    if (!slot_get_path_if_ready((int32_t)s_ble_state.current_page, slot_path, sizeof(slot_path))) {
        ESP_LOGW(TAG, "Page slot pending: idx=%d", (int)s_ble_state.current_page);
        s_ble_state.page_loaded = false;
        return false;
    }

    FILE *f = fopen(slot_path, "rb");
    if (f != NULL) {
        fclose(f);
        s_ble_state.page_loaded = true;
        return true;
    }

    ESP_LOGW(TAG, "Page file not found in slot: %s (idx=%d)", slot_path, (int)s_ble_state.current_page);
    s_ble_state.page_loaded = false;
    return false;
}

static int utf8_char_len(unsigned char c)
{
    if ((c & 0x80u) == 0) return 1;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/**
 * @brief 计算 UTF-8 字符串的显示字符数（含换行符，每个 \n 计 1 字符）
 */
static size_t count_utf8_chars(const char *str)
{
    if (!str) return 0;
    size_t count = 0;
    for (const char *p = str; *p; ) {
        int clen = (*p == '\n') ? 1 : utf8_char_len((unsigned char)*p);
        p += (clen > 0) ? clen : 1;
        count++;
    }
    return count;
}


/**
 * @brief 获取字体布局参数
 */
static void get_text_layout_params(sFONT **font, int *line_height)
{
    *font = display_get_default_ascii_font();
    int font_h = xt_eink_font_get_height();
    *line_height = ((font_h > 0) ? font_h : 25) + 4;  // 字体高度 + 行距
}

/**
 * @brief 通用文本处理：绘制或测量消耗字符数
 * @return 消耗的字符数（含换行符）
 */
/**
 * @brief 通用文本处理：绘制或测量消耗字符数
 * 算法：计算行列布局，逐字填充行缓冲，屏幕满或文件尾则返回绝对 seek 位置
 * @return 绝对位置（就是 seek 到的位置，char_offset + 填充字符数）
 */
static size_t process_wrapped_text(int x, int y, int max_width, int max_height,
                                   const char *text, size_t char_offset, bool do_draw)
{
    if (!text || !*text) return 0;

    sFONT *font;
    int line_height;
    get_text_layout_params(&font, &line_height);
    if (!font) return 0;

    // 跳过 char_offset 个字符到达起始位置（游标就像文件 seek）
    const char *p = text;
    for (size_t i = 0; i < char_offset && *p; i++) {
        int clen = (*p == '\n') ? 1 : utf8_char_len((unsigned char)*p);
        p += (clen > 0) ? clen : 1;
    }

    // 计算布局：能显示多少行
    int lines_can_show = max_height / line_height;
    if (lines_can_show <= 0) lines_can_show = 1;

    char line[256];
    int line_len = 0;
    int lines_filled = 0;
    int cur_y = y;
    size_t consumed = 0;

    // 逐字填充，直到屏幕满或文件结束
    while (*p && lines_filled < lines_can_show) {
        // 换行符：直接换行
        if (*p == '\n') {
            if (do_draw && line_len > 0) {
                line[line_len] = '\0';
                display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
            }
            line_len = 0;
            cur_y += line_height;
            lines_filled++;
            consumed++;  // 换行符计为 1 个字符
            p++;
            continue;
        }

        int clen = utf8_char_len((unsigned char)*p);
        if (clen <= 0) clen = 1;

        // 行缓冲溢出：当前字符放不下，先输出已有的行
        if (line_len + clen >= (int)sizeof(line) - 1) {
            if (do_draw && line_len > 0) {
                line[line_len] = '\0';
                display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
            }
            line_len = 0;
            cur_y += line_height;
            lines_filled++;
            if (lines_filled >= lines_can_show) break;
            continue;
        }

        // 尝试添加字符
        memcpy(line + line_len, p, clen);
        line_len += clen;
        line[line_len] = '\0';

        // 检测超宽：当前字符导致行超出宽度限制
        if (display_get_text_width_font(line, font) > max_width && line_len > clen) {
            // 回退当前字符，输出当前行
            line_len -= clen;
            line[line_len] = '\0';
            if (do_draw && line_len > 0) {
                display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
            }
            line_len = 0;
            cur_y += line_height;
            lines_filled++;
            if (lines_filled >= lines_can_show) break;
            // 不移动 p，下次循环重新处理这个字符
            continue;
        }

        // 字符成功添加，游标往后走
        consumed++;
        p += clen;
    }

    // 输出最后一行（如果还有空间且有内容）
    if (do_draw && line_len > 0 && lines_filled < lines_can_show) {
        line[line_len] = '\0';
        display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
    }

    return char_offset + consumed;
}


static void draw_wrapped_text(int x, int y, int max_width, int max_height,
                              const char *text, size_t char_offset)
{
    process_wrapped_text(x, y, max_width, max_height, text, char_offset, true);
}

static size_t measure_wrapped_consumed(int max_width, int max_height,
                                       const char *text, size_t char_offset)
{
    return process_wrapped_text(0, 0, max_width, max_height, text, char_offset, false);
}

/**
 * @brief 蓝牙连接状态回调
 */
static void ble_connect_callback(bool connected)
{
    // 在页面切换/清理过程中，回调可能被注销，此时不应执行
    screen_t *current_screen = screen_manager_get_current();
    if (current_screen == NULL || current_screen != &g_ble_reader_screen) {
        ESP_LOGW(TAG, "BLE callback triggered but screen is not active, ignoring");
        return;
    }

    if (connected) {
        ESP_LOGI(TAG, "BLE device connected!");
        s_ble_state.state = BLE_READER_STATE_CONNECTED;
        s_ble_state.device_connected = true;
    } else {
        ESP_LOGI(TAG, "BLE device disconnected");
        s_ble_state.state = BLE_READER_STATE_IDLE;
        s_ble_state.device_connected = false;
    }

    if (current_screen != NULL && current_screen == &g_ble_reader_screen) {
        current_screen->needs_redraw = true;
        // 立即触发屏幕刷新，显示连接状态变化
        screen_manager_draw();
    }
}

// X4IM 协议接收状态 - 改用流式写入文件，避免大内存分配
static struct {
    bool receiving;
    uint32_t expected_size;
    uint32_t received_size;
    FILE *file_handle;          // 直接写入文件，不使用内存缓冲区
    char filename[64];          // 当前写入的文件名
    uint16_t current_page;
    int32_t logical_index;      // 对应的滑动窗口索引（可为负）
    int slot_id;                // 当前写入的槽位 ID
    bool use_sd_card;           // 是否写入SD卡（传输模式）
    uint16_t flags;             // X4IM v2 flags
} x4im_rx_state = {.logical_index = -1, .slot_id = -1};

// X4IM v2 协议常量
#define X4IM_HEADER_SIZE        32      // v2 帧头长度
#define X4IM_HEADER_SIZE_V1     12      // v1 帧头长度（兼容）
#define X4IM_FLAGS_STORAGE_SD   0x0100  // Bit 8: 存储到SD卡

// 线程安全保护互斥锁
static SemaphoreHandle_t x4im_rx_mutex = NULL;

/**
 * @brief 初始化 X4IM 接收互斥锁
 */
static bool init_x4im_mutex(void)
{
    if (x4im_rx_mutex == NULL) {
        x4im_rx_mutex = xSemaphoreCreateMutex();
        if (x4im_rx_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create X4IM mutex");
            return false;
        }
    }
    return true;
}

/**
 * @brief 释放 X4IM 接收互斥锁和清理接收状态
 */
static void deinit_x4im_mutex(void)
{
    // 关闭可能正在进行的文件传输
    if (x4im_rx_state.file_handle != NULL) {
        fclose(x4im_rx_state.file_handle);
        x4im_rx_state.file_handle = NULL;
        ESP_LOGW(TAG, "Closed incomplete file transfer during cleanup");
    }
    
    // 重置接收状态
    x4im_rx_state.receiving = false;
    x4im_rx_state.expected_size = 0;
    x4im_rx_state.received_size = 0;
    x4im_rx_state.logical_index = -1;
    x4im_rx_state.slot_id = -1;
    
    // 删除互斥锁
    if (x4im_rx_mutex != NULL) {
        vSemaphoreDelete(x4im_rx_mutex);
        x4im_rx_mutex = NULL;
    }
}

/**
 * @brief 递归扫描目录，统计文件和目录数量
 * @note 限制递归深度避免栈溢出，使用堆分配路径缓冲避免占用栈空间
 */
static void scan_directory_recursive(const char *path, int *file_count, int *dir_count, uint64_t *used_size)
{
    // 限制递归深度，避免栈溢出（最大10层）
    static int depth = 0;
    if (depth > 10) {
        ESP_LOGW(TAG, "Directory scan depth limit reached at: %s", path);
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) return;

    depth++;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 使用堆分配路径缓冲，避免栈溢出
        char *full_path = malloc(512);
        if (!full_path) {
            ESP_LOGE(TAG, "Failed to allocate path buffer during scan");
            break;
        }
        snprintf(full_path, 512, "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                (*dir_count)++;
                scan_directory_recursive(full_path, file_count, dir_count, used_size);
            } else {
                (*file_count)++;
                (*used_size) += st.st_size;
            }
        }
        free(full_path);
    }
    closedir(dir);
    depth--;
}

// 前向声明
static void handle_page_data(const uint8_t *data, size_t length);

/**
 * @brief 蓝牙数据接收回调 - 支持 X4IM 位图协议（流式写入文件）
 */
static void ble_data_received_callback(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        ESP_LOGW(TAG, "Received NULL or empty data");
        if (data != NULL) {
            free((void *)data);  // 释放空数据包内存
        }
        return;
    }

    // 检查屏幕是否仍然激活（防止在清理过程中处理数据）
    screen_t *current_screen = screen_manager_get_current();
    if (current_screen == NULL || current_screen != &g_ble_reader_screen) {
        ESP_LOGW(TAG, "BLE data received but screen is not active, discarding %u bytes", length);
        free((void *)data);
        return;
    }

    ESP_LOGI(TAG, "===== BLE DATA RECEIVED: %u bytes =====", length);
    ESP_LOGI(TAG, "First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X",
             data[0], data[1], data[2], data[3]);

    // ========== 检查魔术头（命令包） ==========
    // 所有命令包必须以 [0xA5, 0x5A, CMD, ...] 开头
    bool is_command = false;
    if (length >= 3 && data[0] == X4IM_MAGIC_HEADER_0 && data[1] == X4IM_MAGIC_HEADER_1) {
        is_command = true;
        ESP_LOGI(TAG, "Detected command packet, CMD=0x%02X", data[2]);
    } else {
        ESP_LOGI(TAG, "Detected data packet (no magic header), treating as page data");
    }

    // 如果不是命令包，按普通数据处理（页面数据）
    if (!is_command) {
        // 旧的页面数据处理逻辑
        if (s_ble_state.work_mode != BLE_MODE_READING) {
            ESP_LOGW(TAG, "Data packet received but not in READING mode, discarding");
            free((void *)data);
            return;
        }

        // 将数据写入槽位（handle_page_data 会负责释放 data 内存）
        handle_page_data(data, length);
        return;  // handle_page_data 已经释放了 data，这里不需要再释放
    }

    // 以下是命令处理逻辑，跳过魔术头
    const uint8_t *cmd_data = &data[2];  // 跳过 [0xA5, 0x5A]
    size_t cmd_length = length - 2;
    uint8_t cmd = cmd_data[0];

    // ========== 处理模式控制命令 ==========
    // SET_MODE 命令：[0xA5, 0x5A, 0x8C, mode] 共4字节
    if (cmd_length == 2 && cmd == X4IM_CMD_SET_MODE) {
        uint8_t new_mode = cmd_data[1];
        ESP_LOGI(TAG, "Received SET_MODE command: %s",
                 new_mode == BLE_MODE_READING ? "READING" : "TRANSFER");
        
        // 切换模式
        if (new_mode == BLE_MODE_READING || new_mode == BLE_MODE_TRANSFER) {
            ble_reader_set_mode((ble_work_mode_t)new_mode);
            
            // 发送确认响应 [0x8D, mode]
            uint8_t response[2] = {X4IM_CMD_GET_MODE, s_ble_state.work_mode};
            ble_manager_send_data(response, 2);
            ESP_LOGI(TAG, "Mode set to %s, sent confirmation",
                     s_ble_state.work_mode == BLE_MODE_READING ? "READING" : "TRANSFER");
        } else {
            ESP_LOGW(TAG, "Invalid mode value: %u", new_mode);
        }
        
        free((void *)data);
        return;
    }
    
    // GET_MODE 命令：[0xA5, 0x5A, 0x8D] 共3字节
    if (cmd_length == 1 && cmd == X4IM_CMD_GET_MODE) {
        ESP_LOGI(TAG, "Received GET_MODE query");
        
        // 发送当前模式 [0x8D, mode]
        uint8_t response[2] = {X4IM_CMD_GET_MODE, s_ble_state.work_mode};
        ble_manager_send_data(response, 2);
        ESP_LOGI(TAG, "Sent current mode: %s",
                 s_ble_state.work_mode == BLE_MODE_READING ? "READING" : "TRANSFER");
        
        free((void *)data);
        return;
    }

    // ========== 设置书籍/章节哈希 ==========
    // SET_BOOK_CHAPTER: [0xA5, 0x5A, 0x98, bookHash(4B,LE), chapterHash(4B,LE)] 共11字节
    if (cmd_length == 9 && cmd == X4IM_CMD_SET_BOOK_CHAPTER) {
        uint32_t book_hash = (uint32_t)cmd_data[1] | ((uint32_t)cmd_data[2] << 8) |
                             ((uint32_t)cmd_data[3] << 16) | ((uint32_t)cmd_data[4] << 24);
        uint32_t chapter_hash = (uint32_t)cmd_data[5] | ((uint32_t)cmd_data[6] << 8) |
                                ((uint32_t)cmd_data[7] << 16) | ((uint32_t)cmd_data[8] << 24);

        bool changed = (book_hash != s_ble_state.current_book_hash) ||
                       (chapter_hash != s_ble_state.current_chapter_hash);

        s_ble_state.current_book_hash = book_hash;
        s_ble_state.current_chapter_hash = chapter_hash;
        s_ble_state.current_book_id = (uint16_t)(book_hash & 0xFFFF); // 兼容旧字段
        s_ble_state.current_page = 0;
        s_ble_state.char_position = 0;

        if (changed) {
            ESP_LOGI(TAG, "Set book/chapter hash: book=0x%08" PRIx32 ", chapter=0x%08" PRIx32,
                     book_hash, chapter_hash);
            // 重建槽文件，避免旧书数据污染
            slot_init();
            slot_prepare_window(0);
            // 发送一次章节快照给 Client，帮助其预加载
            send_position_snapshot();
        }

        free((void *)data);
        return;
    }

    // ========== SD 卡文件管理命令 ==========
    // LIST_FILES 命令：扫描 /sdcard/ 根目录
    // 格式: [0xA5, 0x5A, 0x8E, 路径长度, 路径...] - 可选路径，默认 /sdcard/
    if (cmd_length >= 1 && cmd == X4IM_CMD_LIST_FILES) {
        const char *scan_path = "/sdcard";
        char path_buf[256] = {0};

        if (cmd_length > 1) {
            // 有路径参数： [0xA5, 0x5A, 0x8E, 路径长度, 路径...]
            int path_len = cmd_data[1];
            if (path_len > cmd_length - 2) path_len = cmd_length - 2;
            if (path_len > sizeof(path_buf) - 1) path_len = sizeof(path_buf) - 1;
            memcpy(path_buf, &cmd_data[2], path_len);
            path_buf[path_len] = '\0';
            if (path_len > 0) {
                scan_path = path_buf;
            }
        }

        ESP_LOGI(TAG, "Scanning directory: %s", scan_path);

        // 使用共享的 fs_list_dir 进行目录扫描
        fs_file_info_t *files = NULL;
        int count = 0;

        if (!fs_list_dir(scan_path, &files, &count)) {
            ESP_LOGW(TAG, "Failed to list directory: %s", scan_path);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x00};  // 0=empty
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }

        // 发送目录列表：每条消息 [0x8B, 标志(1), 大小(4字节), 文件名...]
        // 标志: bit0=1 表示目录, bit0=0 表示文件
        int file_count = 0;
        int dir_count = 0;

        for (int i = 0; i < count; i++) {
            uint8_t response[300];
            response[0] = X4IM_CMD_FILE_NOTIFY;

            if (files[i].is_directory) {
                // 目录
                response[1] = 0x01;  // 目录标志
                // 大小字段设为0
                response[2] = 0; response[3] = 0; response[4] = 0; response[5] = 0;
                dir_count++;
            } else {
                // 文件
                response[1] = 0x00;  // 文件标志
                uint32_t size = (uint32_t)files[i].size;
                response[2] = (size >> 0) & 0xFF;
                response[3] = (size >> 8) & 0xFF;
                response[4] = (size >> 16) & 0xFF;
                response[5] = (size >> 24) & 0xFF;
                file_count++;
            }

            // 文件名
            int name_len = strlen(files[i].name);
            if (name_len > 255 - 6) name_len = 255 - 6;
            memcpy(&response[6], files[i].name, name_len);

            ble_manager_send_data(response, 6 + name_len);
            ESP_LOGI(TAG, "  %s: %s", files[i].is_directory ? "Dir" : "File", files[i].name);
        }

        // 释放文件列表
        fs_free_file_list(files);

        // 发送文件列表结束标记 [0x8B, 0xFF]
        uint8_t end_marker[2] = {X4IM_CMD_FILE_NOTIFY, 0xFF};
        ble_manager_send_data(end_marker, 2);
        ESP_LOGI(TAG, "List complete: %d files, %d directories", file_count, dir_count);

        free((void *)data);
        return;
    }

    // DELETE_FILE 命令：[0xA5, 0x5A, 0x8F, 路径...]
    // 客户端已发送完整路径（包含/sdcard/）
    if (cmd_length > 1 && cmd == X4IM_CMD_DELETE_FILE) {
        char *filepath = malloc(512);
        if (!filepath) {
            ESP_LOGE(TAG, "Failed to allocate filepath buffer for DELETE_FILE");
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }
        int name_len = cmd_length - 1;
        if (name_len > 511) name_len = 511;
        memcpy(filepath, &cmd_data[1], name_len);
        filepath[name_len] = '\0';

        ESP_LOGI(TAG, "Deleting: %s", filepath);

        struct stat st;
        if (stat(filepath, &st) == 0) {
            int result;
            if (S_ISDIR(st.st_mode)) {
                // 目录：先检查是否为空
                DIR *dir = opendir(filepath);
                int count = 0;
                if (dir) {
                    struct dirent *e;
                    while ((e = readdir(dir)) != NULL) {
                        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
                            count++;
                        }
                    }
                    closedir(dir);
                }
                if (count > 0) {
                    ESP_LOGW(TAG, "Directory not empty: %s (%d items)", filepath, count);
                    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x03};  // 3=directory not empty
                    ble_manager_send_data(response, 2);
                } else {
                    result = rmdir(filepath);
                    if (result == 0) {
                        ESP_LOGI(TAG, "Deleted directory: %s", filepath);
                        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
                        ble_manager_send_data(response, 2);
                    } else {
                        ESP_LOGW(TAG, "Failed to delete directory: %s (errno=%d)", filepath, errno);
                        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
                        ble_manager_send_data(response, 2);
                    }
                }
            } else {
                // 文件
                result = unlink(filepath);
                if (result == 0) {
                    ESP_LOGI(TAG, "Deleted file: %s", filepath);
                    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
                    ble_manager_send_data(response, 2);
                } else {
                    ESP_LOGW(TAG, "Failed to delete file: %s (errno=%d)", filepath, errno);
                    uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
                    ble_manager_send_data(response, 2);
                }
            }
        } else {
            ESP_LOGW(TAG, "File not found: %s", filepath);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x04};  // 4=not found
            ble_manager_send_data(response, 2);
        }

        free(filepath);
        free((void *)data);
        return;
    }

    // CLEAR_BOOKS 命令：[0xA5, 0x5A, 0x91] - 清空 /sdcard/books 目录
    if (cmd_length == 1 && cmd == X4IM_CMD_CLEAR_BOOKS) {
        ESP_LOGW(TAG, "Received CLEAR_BOOKS command - deleting all files in /sdcard/books");

        // 使用 fs_list_dir 获取文件列表
        fs_file_info_t *files = NULL;
        int count = 0;

        if (!fs_list_dir("/sdcard/books", &files, &count)) {
            ESP_LOGW(TAG, "Failed to list /sdcard/books directory");
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }

        int deleted = 0;
        char *filepath = malloc(512);
        if (filepath) {
            for (int i = 0; i < count; i++) {
                if (!files[i].is_directory) {
                    if (fs_build_child_path(filepath, 512, "/sdcard/books", files[i].name)) {
                        if (unlink(filepath) == 0) {
                            deleted++;
                            ESP_LOGI(TAG, "Deleted: %s", filepath);
                        }
                    }
                }
            }
            free(filepath);
        }
        fs_free_file_list(files);
        
        ESP_LOGI(TAG, "Deleted %d files from /sdcard/books", deleted);
        uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
        ble_manager_send_data(response, 2);

        free((void *)data);
        return;
    }

    // CREATE_DIR 命令：[0xA5, 0x5A, 0x92, 路径...]
    // 客户端已发送完整路径，直接创建即可
    if (cmd_length > 1 && cmd == X4IM_CMD_CREATE_DIR) {
        char *dirpath = malloc(512);
        if (!dirpath) {
            ESP_LOGE(TAG, "Failed to allocate dirpath buffer for CREATE_DIR");
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }
        int name_len = cmd_length - 1;
        if (name_len > 511) name_len = 511;
        memcpy(dirpath, &cmd_data[1], name_len);
        dirpath[name_len] = '\0';

        ESP_LOGI(TAG, "Creating directory: %s", dirpath);

        if (mkdir(dirpath, 0755) == 0 || errno == EEXIST) {
            ESP_LOGI(TAG, "Directory created/exists: %s", dirpath);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
            ble_manager_send_data(response, 2);
        } else {
            ESP_LOGW(TAG, "Failed to create directory: %s (errno=%d)", dirpath, errno);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
        }

        free(dirpath);
        free((void *)data);
        return;
    }

    // RENAME_FILE 命令：[0xA5, 0x5A, 0x90, 旧名长度, 旧名..., 新名长度, 新名...]
    // 客户端已发送完整路径（包含/sdcard/）
    if (cmd_length > 2 && cmd == X4IM_CMD_RENAME_FILE) {
        int offset = 1;
        int old_len = cmd_data[offset++];
        if (old_len > cmd_length - offset - 1) old_len = cmd_length - offset - 1;
        if (old_len > 490) old_len = 490;

        char oldpath[512];
        memcpy(oldpath, &cmd_data[offset], old_len);
        oldpath[old_len] = '\0';
        offset += old_len;

        int new_len = cmd_data[offset++];
        if (new_len > cmd_length - offset) new_len = cmd_length - offset;
        if (new_len > 490) new_len = 490;

        char newpath[512];
        memcpy(newpath, &cmd_data[offset], new_len);
        newpath[new_len] = '\0';

        ESP_LOGI(TAG, "Renaming: %s -> %s", oldpath, newpath);

        if (rename(oldpath, newpath) == 0) {
            ESP_LOGI(TAG, "Rename successful");
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
            ble_manager_send_data(response, 2);
        } else {
            ESP_LOGW(TAG, "Rename failed: %s -> %s (errno=%d)", oldpath, newpath, errno);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
        }

        free((void *)data);
        return;
    }

    // GET_STORAGE_INFO 命令：[0xA5, 0x5A, 0x93]
    if (cmd_length == 1 && cmd == X4IM_CMD_GET_STORAGE_INFO) {
        ESP_LOGI(TAG, "Received GET_STORAGE_INFO command - scanning entire SD card");

        // 递归扫描 /sdcard/ 目录
        uint64_t total_size = 0;
        uint64_t used_size = 0;
        int file_count = 0;
        int dir_count = 0;

        // 使用 statfs 获取实际容量
        FATFS *fs;
        DWORD fre_clust, fre_bsec, tot_bsec;
        if (f_getfree("/sdcard", &fre_clust, &fs) == 0) {
            tot_bsec = fs->csize * fs->n_fatent;
            fre_bsec = fre_clust * fs->csize;
            total_size = (uint64_t)tot_bsec * 512;
            uint64_t free_size = (uint64_t)fre_bsec * 512;
            used_size = total_size - free_size;
        } else {
            // 估算总容量（假设 4GB SD 卡）
            total_size = 4LL * 1024 * 1024 * 1024;
        }

        // 递归统计文件和目录数量
        scan_directory_recursive("/sdcard", &file_count, &dir_count, &used_size);

        // 发送响应: [0x93, total(4), used(4), files(2), dirs(2)]
        uint8_t response[13];
        response[0] = X4IM_CMD_GET_STORAGE_INFO;

        // total_size (小端，限制为 32 位)
        uint32_t total32 = (total_size > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)total_size;
        response[1] = (total32 >> 0) & 0xFF;
        response[2] = (total32 >> 8) & 0xFF;
        response[3] = (total32 >> 16) & 0xFF;
        response[4] = (total32 >> 24) & 0xFF;

        // used_size (小端，限制为 32 位)
        uint32_t used32 = (used_size > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)used_size;
        response[5] = (used32 >> 0) & 0xFF;
        response[6] = (used32 >> 8) & 0xFF;
        response[7] = (used32 >> 16) & 0xFF;
        response[8] = (used32 >> 24) & 0xFF;

        // file_count (小端)
        response[9] = (file_count >> 0) & 0xFF;
        response[10] = (file_count >> 8) & 0xFF;

        // dir_count (小端)
        response[11] = (dir_count >> 0) & 0xFF;
        response[12] = (dir_count >> 8) & 0xFF;

        ble_manager_send_data(response, 13);
        ESP_LOGI(TAG, "Storage info: total=%llu, used=%llu, files=%d, dirs=%d",
                 (unsigned long long)total_size, (unsigned long long)used_size, file_count, dir_count);

        free((void *)data);
        return;
    }

    // READ_FILE 命令：[0xA5, 0x5A, 0x94, 偏移(4字节), 大小(4字节), 路径...]
    // 读取文件的指定范围并发送给客户端
    if (cmd_length > 9 && cmd == X4IM_CMD_READ_FILE) {
        uint32_t offset = cmd_data[1] | (cmd_data[2] << 8) | (cmd_data[3] << 16) | (cmd_data[4] << 24);
        uint32_t read_size = cmd_data[5] | (cmd_data[6] << 8) | (cmd_data[7] << 16) | (cmd_data[8] << 24);
        
        char filepath[512];
        int path_len = cmd_length - 9;
        if (path_len > sizeof(filepath) - 1) path_len = sizeof(filepath) - 1;
        memcpy(filepath, &cmd_data[9], path_len);
        filepath[path_len] = '\0';

        ESP_LOGI(TAG, "Reading file: %s (offset=%u, size=%u)", filepath, offset, read_size);

        FILE *file = fopen(filepath, "rb");
        if (!file) {
            ESP_LOGW(TAG, "Failed to open file: %s (errno=%d)", filepath, errno);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x04};  // 4=not found
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }

        // 获取文件大小
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // 检查偏移和大小是否有效
        if (offset >= file_size) {
            ESP_LOGW(TAG, "Invalid offset: %u >= %ld", offset, file_size);
            fclose(file);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }

        // 调整读取大小
        if (offset + read_size > file_size) {
            read_size = file_size - offset;
        }

        // 分块发送文件数据（每块最大256字节）
        const uint32_t CHUNK_SIZE = 256;
        uint8_t *chunk_buffer = malloc(CHUNK_SIZE + 13);  // 13字节头部
        if (!chunk_buffer) {
            ESP_LOGE(TAG, "Failed to allocate chunk buffer");
            fclose(file);
            free((void *)data);
            return;
        }

        fseek(file, offset, SEEK_SET);
        uint32_t sent = 0;

        while (sent < read_size) {
            uint32_t chunk_len = (read_size - sent > CHUNK_SIZE) ? CHUNK_SIZE : (read_size - sent);
            size_t bytes_read = fread(&chunk_buffer[13], 1, chunk_len, file);
            
            if (bytes_read == 0) {
                ESP_LOGW(TAG, "File read error or EOF");
                break;
            }

            // 构建数据包: [0x95, 偏移(4), 总大小(4), 当前块大小(4), 数据...]
            chunk_buffer[0] = X4IM_CMD_FILE_DATA;
            uint32_t current_offset = offset + sent;
            chunk_buffer[1] = (current_offset >> 0) & 0xFF;
            chunk_buffer[2] = (current_offset >> 8) & 0xFF;
            chunk_buffer[3] = (current_offset >> 16) & 0xFF;
            chunk_buffer[4] = (current_offset >> 24) & 0xFF;
            chunk_buffer[5] = (file_size >> 0) & 0xFF;
            chunk_buffer[6] = (file_size >> 8) & 0xFF;
            chunk_buffer[7] = (file_size >> 16) & 0xFF;
            chunk_buffer[8] = (file_size >> 24) & 0xFF;
            chunk_buffer[9] = (bytes_read >> 0) & 0xFF;
            chunk_buffer[10] = (bytes_read >> 8) & 0xFF;
            chunk_buffer[11] = (bytes_read >> 16) & 0xFF;
            chunk_buffer[12] = (bytes_read >> 24) & 0xFF;

            ble_manager_send_data(chunk_buffer, 13 + bytes_read);
            sent += bytes_read;

            ESP_LOGI(TAG, "Sent chunk: offset=%u, size=%zu, total=%u/%ld",
                     current_offset, bytes_read, sent, file_size);

            // 短暂延迟，避免蓝牙缓冲区溢出
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        free(chunk_buffer);
        fclose(file);
        
        ESP_LOGI(TAG, "File transfer complete: %u bytes sent", sent);
        
        // 发送传输完成标记
        uint8_t end_marker[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
        ble_manager_send_data(end_marker, 2);

        free((void *)data);
        return;
    }

    // ========== 未识别的命令 ==========
    ESP_LOGW(TAG, "Unknown command in packet: CMD=0x%02X, length=%zu", cmd, cmd_length);
    free((void *)data);
}

/**
 * @brief 处理页面数据（非命令数据）
 * @param data 数据指针（会在函数内部释放）
 * @param length 数据长度
 */
static void __attribute__((unused)) handle_page_data(const uint8_t *data, size_t length)
{
    // 检查是否是 X4IM v2 帧头（32 字节）
    // "X4IM" + version(2) + flags(2) + payload_size(4) + sequence(2) + reserved(2) + filename(16)
    if (length >= X4IM_HEADER_SIZE && data[0] == 'X' && data[1] == '4' &&
        data[2] == 'I' && data[3] == 'M' && data[4] == 0x02) {

        // 解析帧头
        uint16_t flags = data[6] | (data[7] << 8);
        uint32_t payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
        
        ESP_LOGI(TAG, "X4IM v2 header: payload_size=%" PRIu32, payload_size);
        
        // 提取文件名（偏移 16，最多 15 字符）
        char recv_filename[16] = {0};
        memcpy(recv_filename, &data[16], 15);
        recv_filename[15] = '\0';
        
        // 检查是否需要存储到 SD 卡
        bool use_sd = (flags & X4IM_FLAGS_STORAGE_SD) != 0;
        
        ESP_LOGI(TAG, "X4IM frame: v2, flags=0x%04X, payload=%" PRIu32 ", sd=%d, name='%s'",
             flags, payload_size, use_sd, recv_filename);

        // ========== 模式检查 ==========
        // 阅读模式下，拒绝 SD 卡写入
        if (use_sd && s_ble_state.work_mode == BLE_MODE_READING) {
            ESP_LOGW(TAG, "SD write rejected in READING mode");
            free((void *)data);
            return;
        }
        
        // 传输模式下，必须使用 SD 卡
        if (s_ble_state.work_mode == BLE_MODE_TRANSFER && !use_sd) {
            // 自动升级为 SD 存储
            use_sd = true;
            ESP_LOGI(TAG, "Auto-upgrade to SD storage in TRANSFER mode");
        }

        // 获取互斥锁，初始化接收状态（流式写入）
        if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 关闭旧文件（如果有未完成的传输）
            if (x4im_rx_state.file_handle != NULL) {
                fclose(x4im_rx_state.file_handle);
                x4im_rx_state.file_handle = NULL;
                ESP_LOGW(TAG, "Closed incomplete file transfer");
            }

            // 准备文件路径
            x4im_rx_state.use_sd_card = use_sd;
            x4im_rx_state.flags = flags;
            x4im_rx_state.slot_id = -1;
            x4im_rx_state.logical_index = -1;

            if (use_sd) {
                // ========== SD 卡存储（传输模式）==========
                // 创建 /sdcard/books 目录（如果不存在）
                mkdir("/sdcard/books", 0755);
                
                // SD 卡路径：使用接收到的文件名，添加扩展名
                if (recv_filename[0] != '\0') {
                    // 根据 flags 推断文件类型添加扩展名
                    const char *ext = ".bin";  // 默认二进制
                    if (flags & 0x0001) ext = ".pdf";      // bit 0: PDF
                    else if (flags & 0x0002) ext = ".epub"; // bit 1: EPUB
                    else if (flags & 0x0004) ext = ".txt";  // bit 2: TXT
                    else if (flags & 0x0008) ext = ".png";  // bit 3: PNG
                    else if (flags & 0x0010) ext = ".jpg";  // bit 4: JPG
                    else if (flags & 0x0020) ext = ".bmp";  // bit 5: BMP
                    
                    snprintf(x4im_rx_state.filename, sizeof(x4im_rx_state.filename),
                             "/sdcard/books/%s%s", recv_filename, ext);
                } else {
                    // 默认文件名
                    snprintf(x4im_rx_state.filename, sizeof(x4im_rx_state.filename),
                             "/sdcard/books/book_%04x_%05u.bin",
                             s_ble_state.current_book_id ? s_ble_state.current_book_id : 1,
                             s_ble_state.transfer_file_count);
                }
            } else {
                // ========== LittleFS 存储（阅读模式三槽队列）==========
                // 从文件名解析逻辑索引（必须包含有效索引）
                int32_t incoming_index = INT32_MIN;
                if (!parse_index_from_name(recv_filename, &incoming_index)) {
                    ESP_LOGE(TAG, "Invalid filename for LittleFS (must be 'page_{index}'): %s", recv_filename);
                    xSemaphoreGive(x4im_rx_mutex);
                    free((void *)data);
                    return;
                }
                
                // LittleFS 路径：三槽队列，槽位 = idx mod 3
                if (!ensure_slot_dir()) {
                    xSemaphoreGive(x4im_rx_mutex);
                    free((void *)data);
                    return;
                }

                int slot_id = slot_id_from_index(incoming_index);
                x4im_rx_state.slot_id = slot_id;
                x4im_rx_state.logical_index = incoming_index;

                ble_slot_entry_t *slot = &s_slots[slot_id];
                // 复用槽位前先清理旧文件并重置状态
                unlink(slot->path);
                slot->logical_index = incoming_index;
                slot->ready = false;

                snprintf(x4im_rx_state.filename, sizeof(x4im_rx_state.filename), "%s", slot->path);
            }

            // 打开文件准备流式写入
            x4im_rx_state.file_handle = fopen(x4im_rx_state.filename, "wb");
            if (x4im_rx_state.file_handle == NULL) {
                xSemaphoreGive(x4im_rx_mutex);
                ESP_LOGE(TAG, "Failed to open file for streaming: %s (errno=%d)", x4im_rx_state.filename, errno);
                free((void *)data);  // 释放内存
                return;
            }

            ESP_LOGI(TAG, "Opened file for streaming: %s (SD=%d)", x4im_rx_state.filename, use_sd);

            x4im_rx_state.expected_size = payload_size;
            x4im_rx_state.received_size = 0;
            x4im_rx_state.receiving = true;
            
            // 传输模式：更新进度信息
            if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                s_ble_state.transfer_bytes_total = payload_size;
                s_ble_state.transfer_bytes_received = 0;
                strncpy(s_ble_state.transfer_filename, 
                        recv_filename[0] ? recv_filename : "unknown",
                        sizeof(s_ble_state.transfer_filename) - 1);
                
                // 记录接收到的原始文件名（可能被截断）到SD卡日志文件
                FILE *log_file = fopen("/sdcard/transfer_log.txt", "a");
                if (log_file != NULL) {
                        fprintf(log_file, "File: %s | Path: %s | Size: %" PRIu32 " bytes\n",
                            recv_filename[0] ? recv_filename : "unknown",
                            x4im_rx_state.filename,
                            payload_size);
                    fclose(log_file);
                }
            }

            // 如果帧头后还有数据，直接写入文件
            const int header_size = X4IM_HEADER_SIZE;  // v2 固定 32 字节
            if (length > header_size) {
                uint32_t copy_len = length - header_size;
                if (copy_len > payload_size) {
                    copy_len = payload_size;
                }
                
                size_t written = fwrite(data + header_size, 1, copy_len, x4im_rx_state.file_handle);
                if (written != copy_len) {
                    ESP_LOGE(TAG, "File write error: expected %" PRIu32 ", wrote %lu", copy_len, (unsigned long)written);
                    fclose(x4im_rx_state.file_handle);
                    x4im_rx_state.file_handle = NULL;
                    x4im_rx_state.receiving = false;
                    xSemaphoreGive(x4im_rx_mutex);
                    free((void *)data);  // 释放内存
                    return;
                }
                
                x4im_rx_state.received_size = copy_len;
                
                // 传输模式：更新已接收字节数
                if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                    s_ble_state.transfer_bytes_received = copy_len;
                }
                
                ESP_LOGI(TAG, "Wrote %" PRIu32 " bytes from header packet (%" PRIu32 "/%" PRIu32 ")",
                         copy_len, x4im_rx_state.received_size, x4im_rx_state.expected_size);
            }

            // 检查是否已完成（单包传输完成）
            bool complete = (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            
            if (complete) {
                fclose(x4im_rx_state.file_handle);
                x4im_rx_state.file_handle = NULL;
                x4im_rx_state.receiving = false;
                
                ESP_LOGI(TAG, "======== FILE SAVED ========");
                ESP_LOGI(TAG, "File: %s", x4im_rx_state.filename);
                ESP_LOGI(TAG, "Size: %" PRIu32 " bytes", x4im_rx_state.received_size);
                ESP_LOGI(TAG, "Storage: %s", x4im_rx_state.use_sd_card ? "SD Card" : "LittleFS");
                ESP_LOGI(TAG, "============================");

                if (!x4im_rx_state.use_sd_card && x4im_rx_state.slot_id >= 0) {
                    slot_mark_ready(x4im_rx_state.slot_id, x4im_rx_state.logical_index);
                }

                xSemaphoreGive(x4im_rx_mutex);

                // ========== 根据模式执行不同的后处理 ==========
                if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                    // 传输模式：更新文件计数，更新状态
                    s_ble_state.transfer_file_count++;
                    s_ble_state.transfer_bytes_received = x4im_rx_state.received_size;
                    s_ble_state.state = BLE_READER_STATE_READY;  // 传输完成，准备下一个
                    ESP_LOGI(TAG, "Transfer mode: file #%u saved to SD card", s_ble_state.transfer_file_count);
                } else {
                    // 阅读模式：保持原有逻辑
                    // 收到第一帧数据时初始化 book_id
                    if (!s_ble_state.initialization_complete) {
                        if (s_ble_state.current_book_id == 0) {
                            s_ble_state.current_book_id = 1;  // 默认书籍 ID
                            ESP_LOGI(TAG, "First page received, book_id set to %04x", s_ble_state.current_book_id);
                        }
                        s_ble_state.showing_confirm_prompt = true;
                        s_ble_state.current_page = 0;
                        ESP_LOGI(TAG, "Showing confirm prompt: Click CONFIRM to start reading");
                    }

                    // 标记页面已加载
                    s_ble_state.page_loaded = true;
                }

                // 触发重绘
                screen_t *screen = screen_manager_get_current();
                if (screen != NULL && screen == &g_ble_reader_screen) {
                    screen->needs_redraw = true;
                    screen_manager_draw();
                }
                
                // header packet 处理完成，释放内存并返回
                free((void *)data);
                return;
            } else {
                xSemaphoreGive(x4im_rx_mutex);
                // 还需要继续接收，释放内存并返回
                free((void *)data);
                return;
            }
        }
        
        // header packet 处理失败分支也要释放并返回
        free((void *)data);
        return;
    }

    // 继续接收位图数据 - 流式写入文件
    if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (x4im_rx_state.receiving && x4im_rx_state.file_handle != NULL) {
            uint32_t remaining = x4im_rx_state.expected_size - x4im_rx_state.received_size;
            uint32_t copy_len = (length > remaining) ? remaining : length;

            // 直接写入文件，无需内存缓冲区
            size_t written = fwrite(data, 1, copy_len, x4im_rx_state.file_handle);
            if (written != copy_len) {
                ESP_LOGE(TAG, "File write error: expected %" PRIu32 ", wrote %lu", copy_len, (unsigned long)written);
                fclose(x4im_rx_state.file_handle);
                x4im_rx_state.file_handle = NULL;
                x4im_rx_state.receiving = false;
                xSemaphoreGive(x4im_rx_mutex);
                // 错误处理：释放内存并返回
                free((void *)data);
                return;
            }

            x4im_rx_state.received_size += copy_len;
            
            // 传输模式：更新进度
            if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                s_ble_state.transfer_bytes_received = x4im_rx_state.received_size;
            }

            // 每 10 个包或传输完成时打印详细进度
            static uint32_t last_log_bytes = 0;
            static uint32_t last_ui_update_bytes = 0;
            bool should_log = (x4im_rx_state.received_size - last_log_bytes >= 2440) || 
                              (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            
            // 传输模式：每接收 5% 或传输完成时刷新界面
            bool should_update_ui = false;
            if (s_ble_state.work_mode == BLE_MODE_TRANSFER && x4im_rx_state.expected_size > 0) {
                uint32_t progress_diff = x4im_rx_state.received_size - last_ui_update_bytes;
                uint32_t update_threshold = x4im_rx_state.expected_size / 20;  // 5%
                should_update_ui = (progress_diff >= update_threshold) || 
                                   (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            }
            
            if (should_log) {
                ESP_LOGI(TAG, "Streaming to file: %" PRIu32 "/%" PRIu32 " bytes (%.1f%%) | UI: %" PRIu32 "/%" PRIu32,
                         x4im_rx_state.received_size, x4im_rx_state.expected_size,
                         (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size,
                         s_ble_state.transfer_bytes_received, s_ble_state.transfer_bytes_total);
                last_log_bytes = x4im_rx_state.received_size;
            }
            
            // 刷新传输模式界面
            if (should_update_ui) {
                last_ui_update_bytes = x4im_rx_state.received_size;
                ESP_LOGI(TAG, "Updating transfer UI (%.1f%%)", 
                         (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size);
                xSemaphoreGive(x4im_rx_mutex);  // 临时释放锁
                
                // 只刷新传输进度部分，不全刷屏幕
                draw_transfer_mode_screen();
                display_refresh(REFRESH_MODE_PARTIAL);
                
                xSemaphoreTake(x4im_rx_mutex, portMAX_DELAY);  // 重新获取锁
            }

            // 检查是否完成
            bool complete = (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            
            if (complete) {
                fclose(x4im_rx_state.file_handle);
                x4im_rx_state.file_handle = NULL;
                x4im_rx_state.receiving = false;
                
                ESP_LOGI(TAG, "======== FILE RECEPTION COMPLETE ========");
                ESP_LOGI(TAG, "File: %s", x4im_rx_state.filename);
                ESP_LOGI(TAG, "Size: %" PRIu32 " bytes", x4im_rx_state.received_size);
                ESP_LOGI(TAG, "Storage: %s", x4im_rx_state.use_sd_card ? "SD Card" : "LittleFS");
                ESP_LOGI(TAG, "=========================================");

                if (!x4im_rx_state.use_sd_card && x4im_rx_state.slot_id >= 0) {
                    slot_mark_ready(x4im_rx_state.slot_id, x4im_rx_state.logical_index);
                }
                
                xSemaphoreGive(x4im_rx_mutex);

                // ========== 根据模式执行不同的后处理 ==========
                if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                    // 传输模式：更新文件计数
                    s_ble_state.transfer_file_count++;
                    s_ble_state.state = BLE_READER_STATE_READY;
                    ESP_LOGI(TAG, "Transfer mode: file #%u saved", s_ble_state.transfer_file_count);
                } else {
                    // 阅读模式：保持原有逻辑
                    // 收到第一帧数据时初始化 book_id
                    if (!s_ble_state.initialization_complete) {
                        if (s_ble_state.current_book_id == 0) {
                            s_ble_state.current_book_id = 1;  // 默认书籍 ID
                            ESP_LOGI(TAG, "First page received, book_id set to %04x", s_ble_state.current_book_id);
                        }
                        s_ble_state.showing_confirm_prompt = true;
                        s_ble_state.current_page = 0;  // 第一帧是页面 0
                        ESP_LOGI(TAG, "Showing confirm prompt: Click CONFIRM to start reading");
                    }

                    if (x4im_rx_state.logical_index == (int32_t)s_ble_state.current_page) {
                        // 只在当前页完成时统计字符数（不包含换行符）
                        char filename[64];
                        snprintf(filename, sizeof(filename), "%s", x4im_rx_state.filename);
                        FILE *f = fopen(filename, "rb");
                        if (f != NULL) {
                            static char text_buf[BLE_TEXT_MAX_BYTES];
                            size_t read = fread(text_buf, 1, sizeof(text_buf) - 1, f);
                            fclose(f);
                            text_buf[read] = '\0';
                            s_ble_state.total_chars = count_utf8_chars(text_buf);
                            ESP_LOGI(TAG, "Page loaded: %zu display characters (excluding newlines)", s_ble_state.total_chars);
                        } else {
                            s_ble_state.total_chars = 0;
                            ESP_LOGW(TAG, "Failed to count characters in page");
                        }
                    }

                    // 标记页面已加载（仅对当前页）
                    s_ble_state.page_loaded = (x4im_rx_state.logical_index == (int32_t)s_ble_state.current_page);
                }

                // 触发重绘
                screen_t *screen = screen_manager_get_current();
                if (screen != NULL && screen == &g_ble_reader_screen) {
                    screen->needs_redraw = true;
                    screen_manager_draw();
                }
                
                // 完成接收，释放内存并返回
                free((void *)data);
                return;
            } else {
                // 传输模式：定期触发重绘以更新进度
                if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                    static uint32_t last_redraw_bytes = 0;
                    // 每接收 10KB 或更多时触发一次重绘
                    if (x4im_rx_state.received_size - last_redraw_bytes >= 10240) {
                        last_redraw_bytes = x4im_rx_state.received_size;
                        screen_t *screen = screen_manager_get_current();
                        if (screen != NULL && screen == &g_ble_reader_screen) {
                            screen->needs_redraw = true;
                        }
                    }
                }
                xSemaphoreGive(x4im_rx_mutex);
                // 继续接收中，释放内存并返回
                free((void *)data);
                return;
            }
        } else {
            xSemaphoreGive(x4im_rx_mutex);
            // 未在接收状态，释放内存并返回
            free((void *)data);
            return;
        }
    }
    
    // 无法获取互斥锁，释放内存并返回
    free((void *)data);
}

/**
 * @brief 页面就绪回调
 */
static bool on_page_ready(uint16_t book_id, uint16_t page_num)
{
    ESP_LOGI(TAG, "Page ready: book=%04x, page=%u", book_id, page_num);
    
    if (book_id == s_ble_state.current_book_id && 
        page_num == s_ble_state.current_page) {
        // 当前需要显示的页面已就绪
        if (load_current_page()) {
            screen_t *screen = screen_manager_get_current();
            if (screen != NULL && screen == &g_ble_reader_screen) {
                screen->needs_redraw = true;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 防抖定时器回调 - 已废弃，改为按键立即翻页
 * 保留此函数以兼容现有代码结构
 */

static void send_page_sync_notification(uint16_t page_num)
{
    // Construct page notification message
    char msg[32];
    snprintf(msg, sizeof(msg), "PAGE:%u", page_num);

    ESP_LOGI(TAG, "Sending page notification: %s", msg);

    // Send via BLE
    bool sent = ble_manager_send_notification((const uint8_t *)msg, strlen(msg));
    ESP_LOGI(TAG, "Page notification send result: %s", sent ? "SUCCESS" : "FAILED");
}

/**
 * @brief 发送章节快照到 Client（以哈希标识）
 * 格式：[0x97, bookHash(4B,LE), chapterHash(4B,LE)] 共9字节
 */
static void send_position_snapshot(void)
{
    uint8_t snapshot[9];
    snapshot[0] = X4IM_CMD_POSITION_SNAPSHOT;

    uint32_t book_hash = s_ble_state.current_book_hash;
    snapshot[1] = (book_hash >> 0) & 0xFF;
    snapshot[2] = (book_hash >> 8) & 0xFF;
    snapshot[3] = (book_hash >> 16) & 0xFF;
    snapshot[4] = (book_hash >> 24) & 0xFF;

    uint32_t chap_hash = s_ble_state.current_chapter_hash;
    snapshot[5] = (chap_hash >> 0) & 0xFF;
    snapshot[6] = (chap_hash >> 8) & 0xFF;
    snapshot[7] = (chap_hash >> 16) & 0xFF;
    snapshot[8] = (chap_hash >> 24) & 0xFF;

    bool sent = ble_manager_send_data(snapshot, sizeof(snapshot));
    ESP_LOGI(TAG, "[POSITION_SNAPSHOT] book=0x%08" PRIx32 ", chapter=0x%08" PRIx32 " - %s",
             book_hash, chap_hash, sent ? "SENT" : "FAILED");
}

/**
 * @brief 更新三页缓存窗口 (prev, current, next)
 * 并检查缓存中是否存在，不存在则向手机请求
 */
static void update_cached_window(uint16_t current_page)
{
    // 更新缓存页码记录
    s_ble_state.cached_pages[0] = (current_page > 0) ? current_page - 1 : 0;
    s_ble_state.cached_pages[1] = current_page;
    s_ble_state.cached_pages[2] = current_page + 1;

    // 重建 3 槽窗口映射：-1,0,+1 对应 slot0/1/2（取模）
    slot_prepare_window((int32_t)current_page);

    bool ready_prev = slot_get_path_if_ready((int32_t)s_ble_state.cached_pages[0], NULL, 0);
    bool ready_curr = slot_get_path_if_ready((int32_t)s_ble_state.cached_pages[1], NULL, 0);
    bool ready_next = slot_get_path_if_ready((int32_t)s_ble_state.cached_pages[2], NULL, 0);

    s_ble_state.page_loaded = ready_curr;

    ESP_LOGI(TAG, "Cache window -> prev:%u(%s) curr:%u(%s) next:%u(%s)",
             s_ble_state.cached_pages[0], ready_prev ? "ready" : "pending",
             s_ble_state.cached_pages[1], ready_curr ? "ready" : "pending",
             s_ble_state.cached_pages[2], ready_next ? "ready" : "pending");
}

/**
 * @brief 清理过期的页面（超出三页范围的）
 * 仅保留 [current-1, current, current+1]
 */
static void __attribute__((unused)) cleanup_old_pages(uint16_t current_page)
{
    if (s_ble_state.current_book_id == 0) return;

    // 此函数在 BLE 接收线程中调用，为了不阻塞太久，我们只尝试清理最近的过期页面
    // 假设翻页是顺序的，我们只需要删除 current-2 和 current+2 (如果有的话)
    
    // 1. 删除前向过期页 (current - 2)
    if (current_page >= 2) {
        uint16_t page_to_delete = current_page - 2;
        char filename[64];
        snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                 s_ble_state.current_book_id, page_to_delete);
        
        struct stat st;
        if (stat(filename, &st) == 0) {
            unlink(filename);
            ESP_LOGI(TAG, "Deleted old page cached file: %s", filename);
        }
    }

    // 2. 删除后向过期页 (current + 2)
    // 注意：如果是向回翻页，后面的页面可能变成了"未来"的页，但为了节省空间，我们只保留紧邻的一页
    // 如果用户跳跃性翻页，可能需要更激进的扫描清理，但这里假设顺序阅读
    uint16_t page_to_delete_next = current_page + 2;
    char filename[64];
    snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
             s_ble_state.current_book_id, page_to_delete_next);
    
    struct stat st;
    if (stat(filename, &st) == 0) {
        unlink(filename);
        ESP_LOGI(TAG, "Deleted future page cached file: %s", filename);
    }
}

/**
 * @brief 预加载需要回调
 */
static void on_preload_needed(uint16_t book_id, uint16_t start_page, uint8_t page_count)
{
    if (!s_ble_state.device_connected) {
        ESP_LOGW(TAG, "Cannot preload: device not connected");
        return;
    }

    if (s_ble_state.preload_requested && 
        s_ble_state.preload_start_page == start_page) {
        // 已经请求过这个范围
        return;
    }

    // 通过蓝牙发送请求包
    uint8_t request_buffer[BLE_REQUEST_PKT_SIZE];
    uint16_t request_len = ble_book_protocol_make_request(book_id, start_page, 
                                                          page_count, request_buffer,
                                                          sizeof(request_buffer));

    if (request_len > 0) {
        // 通过蓝牙管理器发送请求
        if (ble_manager_send_data(request_buffer, request_len)) {
            ESP_LOGI(TAG, "Preload request sent: book=%04x, pages=%u-%u",
                     book_id, start_page, start_page + page_count - 1);
            
            s_ble_state.preload_requested = true;
            s_ble_state.preload_start_page = start_page;
        } else {
            ESP_LOGE(TAG, "Failed to send preload request");
        }
    }
}

static void on_draw(screen_t *screen)
{
    ESP_LOGI(TAG, "on_draw START (mode=%s)", 
             s_ble_state.work_mode == BLE_MODE_READING ? "READING" : "TRANSFER");

    if (s_context == NULL) {
        ESP_LOGW(TAG, "s_context is NULL!");
        return;
    }

    // 清屏
    display_clear(COLOR_WHITE);

    // 根据工作模式绘制不同界面
    if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
        // ==================== 传输模式界面 ====================
        draw_transfer_mode_screen();
    } else {
        // ==================== 阅读模式界面 ====================
        draw_reading_mode_screen(false);
    }

    // 刷新墨水屏显示（使用全刷模式保证显示清晰）
    display_refresh(REFRESH_MODE_FULL);

    ESP_LOGI(TAG, "on_draw END");
}

/**
 * @brief 绘制传输模式界面
 */
static void draw_transfer_mode_screen(void)
{
    // 标题栏
    display_draw_text_menu(20, 20, "蓝牙传书模式", COLOR_BLACK, COLOR_WHITE);

    switch (s_ble_state.state) {
        case BLE_READER_STATE_WAITING:
        case BLE_READER_STATE_IDLE:
            // 等待连接/传输
            display_draw_text_menu(20, 80, "状态: 等待文件传输...", COLOR_BLACK, COLOR_WHITE);
            
            if (s_ble_state.device_connected) {
                display_draw_text_menu(20, 120, "设备已连接", COLOR_BLACK, COLOR_WHITE);
                display_draw_text_menu(20, 160, "等待文件...", COLOR_BLACK, COLOR_WHITE);
            } else {
                display_draw_text_menu(20, 120, "设备未连接", COLOR_BLACK, COLOR_WHITE);
                display_draw_text_menu(20, 160, "请使用手机连接 MFP-EPD", COLOR_BLACK, COLOR_WHITE);
            }
            
            if (s_ble_state.transfer_file_count > 0) {
                char file_count_str[64];
                snprintf(file_count_str, sizeof(file_count_str), 
                         "已接收文件: %u 个", s_ble_state.transfer_file_count);
                display_draw_text_menu(20, 220, file_count_str, COLOR_BLACK, COLOR_WHITE);
            }
            break;

        case BLE_READER_STATE_RECEIVING:
            // 正在接收文件
            display_draw_text_menu(20, 80, "状态: 传输中...", COLOR_BLACK, COLOR_WHITE);
            
            // 显示文件名
            if (s_ble_state.transfer_filename[0] != '\0') {
                char filename_display[96];
                snprintf(filename_display, sizeof(filename_display), "文件: %s", s_ble_state.transfer_filename);
                display_draw_text_menu(20, 120, filename_display, COLOR_BLACK, COLOR_WHITE);
            }
            
            // 显示进度
            if (s_ble_state.transfer_bytes_total > 0) {
                int progress_percent = (s_ble_state.transfer_bytes_received * 100) / s_ble_state.transfer_bytes_total;
                
                // 进度条
                int bar_x = 20;
                int bar_y = 170;
                int bar_width = SCREEN_WIDTH - 40;
                int bar_height = 30;
                
                // 外框
                display_draw_rect(bar_x, bar_y, bar_width, bar_height, COLOR_BLACK, false);
                
                // 填充进度
                int filled_width = (bar_width - 4) * progress_percent / 100;
                if (filled_width > 0) {
                    display_draw_rect(bar_x + 2, bar_y + 2, filled_width, bar_height - 4, COLOR_BLACK, true);
                }
                
                // 进度文字
                char progress_str[64];
                snprintf(progress_str, sizeof(progress_str), "%d%% (%lu/%lu 字节)",
                         progress_percent,
                         (unsigned long)s_ble_state.transfer_bytes_received,
                         (unsigned long)s_ble_state.transfer_bytes_total);
                display_draw_text_menu(20, 220, progress_str, COLOR_BLACK, COLOR_WHITE);
            }
            break;

        case BLE_READER_STATE_READY:
            // 传输完成
            display_draw_text_menu(20, 80, "状态: 传输完成!", COLOR_BLACK, COLOR_WHITE);
            display_draw_text_menu(20, 140, "文件已保存到 SD 卡", COLOR_BLACK, COLOR_WHITE);
            
            if (s_ble_state.transfer_file_count > 0) {
                char file_count_str[64];
                snprintf(file_count_str, sizeof(file_count_str), 
                         "共接收: %u 个文件", s_ble_state.transfer_file_count);
                display_draw_text_menu(20, 180, file_count_str, COLOR_BLACK, COLOR_WHITE);
            }
            
            display_draw_text_menu(20, 240, "确认: 继续传输", COLOR_BLACK, COLOR_WHITE);
            break;

        default:
            display_draw_text_menu(20, 80, "蓝牙传书", COLOR_BLACK, COLOR_WHITE);
            break;
    }
    
    // 底部提示
    display_draw_text_menu(20, SCREEN_HEIGHT - 40, "返回: 退出传输模式", COLOR_BLACK, COLOR_WHITE);
}

/**
 * @brief 计算每屏可显示的字符数
 * @return 字符数
 */
static int calculate_chars_per_screen(void)
{
    int chinese_font_height = xt_eink_font_get_height();
    if (chinese_font_height == 0) {
        chinese_font_height = 25;  // 默认值
    }

    int chinese_font_width = 19;  // 默认值
    xt_eink_glyph_t glyph;
    if (xt_eink_font_get_glyph(0x4E2D, &glyph) && glyph.width > 0) {
        chinese_font_width = glyph.width;
    }

    int line_spacing = 4;
    int font_height = chinese_font_height + line_spacing;

    // 使用统一的内容区域布局常量
    int usable_height = SCREEN_HEIGHT - BLE_CONTENT_Y_START - BLE_CONTENT_BOTTOM_MARGIN;
    int lines_per_screen = usable_height / font_height;
    // 左右各留边距
    int max_width = SCREEN_WIDTH - 2 * BLE_CONTENT_X_MARGIN;
    int chars_per_line = max_width / chinese_font_width;
    int total_chars = lines_per_screen * chars_per_line;

    ESP_LOGI(TAG, "Calculated chars_per_screen: %d (lines=%d, chars_per_line=%d, font_width=%d, font_height=%d)",
             total_chars, lines_per_screen, chars_per_line, chinese_font_width, font_height);

    return total_chars;
}

/**
 * @brief 绘制阅读模式界面
 * @param clear_content 是否清除内容区域（滚动时设为 true）
 */
static void draw_reading_mode_screen(bool clear_content)
{
    // 左上角状态区域（和电池信息一样的高度）
    int top_y = 5;

    // 绘制状态行（左上角）：蓝牙 + 状态 + 页码
    char status_line[64] = "蓝牙";
    const char *status_str = NULL;
    switch (s_ble_state.state) {
        case BLE_READER_STATE_IDLE:
            status_str = "空闲";
            break;
        case BLE_READER_STATE_WAITING:
            status_str = "等待连接";
            break;
        case BLE_READER_STATE_CONNECTING:
            status_str = "连接中";
            break;
        case BLE_READER_STATE_CONNECTED:
            status_str = "已连接";
            break;
        case BLE_READER_STATE_RECEIVING:
            status_str = "接收中";
            break;
        case BLE_READER_STATE_READING:
            status_str = "阅读中";
            break;
        default:
            status_str = "未知";
            break;
    }

    // 拼接状态字符串
    snprintf(status_line + strlen(status_line), sizeof(status_line) - strlen(status_line),
             " %s", status_str);

    // 如果有书籍，拼接页码
    if (s_ble_state.current_book_id != 0) {
        if (s_ble_state.total_pages > 0) {
            snprintf(status_line + strlen(status_line), sizeof(status_line) - strlen(status_line),
                     " %u/%u", s_ble_state.current_page + 1, s_ble_state.total_pages);
        } else {
            snprintf(status_line + strlen(status_line), sizeof(status_line) - strlen(status_line),
                     " %u", s_ble_state.current_page + 1);
        }
    }

    display_draw_text_menu(0, top_y, status_line, COLOR_BLACK, COLOR_WHITE);

    // 显示初始化确认提示（未确认时显示）
    if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
        // 屏幕中央显示确认提示
        int center_y = SCREEN_HEIGHT / 2;
        display_draw_text_menu(20, center_y - 40, "点击确认开始阅读",
                               COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, center_y, "按 确认 键",
                               COLOR_BLACK, COLOR_WHITE);
        s_ble_state.showing_confirm_prompt = true;

        // 底部按键提示
        display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                               "确认: 开始",
                               COLOR_BLACK, COLOR_WHITE);
    } else if (s_ble_state.current_book_id == 0) {
        // 未选择书籍
        display_draw_text_menu(20, 100, "未选择书籍", COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, 140, "等待手机发送内容...", COLOR_BLACK, COLOR_WHITE);
        s_ble_state.showing_confirm_prompt = false;
    } else {
        // 已确认，显示内容
        s_ble_state.showing_confirm_prompt = false;

        // 如果需要清除内容区域（滚动时）
        if (clear_content) {
            int content_width = SCREEN_WIDTH - 2 * BLE_CONTENT_X_MARGIN;
            int content_height = SCREEN_HEIGHT - BLE_CONTENT_Y_START - BLE_CONTENT_BOTTOM_MARGIN;
            display_clear_region(BLE_CONTENT_X_MARGIN, BLE_CONTENT_Y_START, content_width, content_height, COLOR_WHITE);
        }

        // 绘制页面内容（TXT 直接渲染）
        char slot_path[64];
        bool slot_ready = slot_get_path_if_ready((int32_t)s_ble_state.current_page, slot_path, sizeof(slot_path));

        if (slot_ready) {
            // 使用互斥锁防止与 BLE 接收冲突
            static char text_buf[BLE_TEXT_MAX_BYTES];

            if (x4im_rx_mutex != NULL && xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                FILE *f = fopen(slot_path, "rb");
                if (f != NULL) {
                    size_t read = fread(text_buf, 1, sizeof(text_buf) - 1, f);
                    fclose(f);

                    // 确保数据有效且有内容
                    if (read > 0) {
                        text_buf[read] = '\0';

                        // 内容区域：使用统一的布局常量
                        int content_width = SCREEN_WIDTH - 2 * BLE_CONTENT_X_MARGIN;
                        int content_height = SCREEN_HEIGHT - BLE_CONTENT_Y_START - BLE_CONTENT_BOTTOM_MARGIN;
                        // 先计算本页可消耗的显示字符数（用于精确翻页）
                        s_ble_state.last_page_consumed = measure_wrapped_consumed(content_width, content_height, text_buf, s_ble_state.char_position);
                        draw_wrapped_text(BLE_CONTENT_X_MARGIN, BLE_CONTENT_Y_START, content_width, content_height, text_buf, s_ble_state.char_position);
                    } else {
                        // 文件为空，显示提示
                        display_draw_text_menu(20, 100, "文件为空", COLOR_BLACK, COLOR_WHITE);
                    }
                } else {
                    slot_ready = false;  // 文件被移除或打开失败，按等待处理
                }
                xSemaphoreGive(x4im_rx_mutex);
            } else {
                // 获取互斥锁超时或失败，显示等待提示
                display_draw_text_menu(20, 100, "正在加载...", COLOR_BLACK, COLOR_WHITE);
            }
        }

        if (!slot_ready) {
            // 页面缺失或尚未写完，显示等待提示
            display_draw_text_menu(20, 100, "等待加载...", COLOR_BLACK, COLOR_WHITE);

            // 如果正在接收这一页，显示进度
            if (x4im_rx_state.receiving &&
                x4im_rx_state.expected_size > 0 &&
                x4im_rx_state.logical_index == (int32_t)s_ble_state.current_page) {
                char progress[64];
                float percent = (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size;
                snprintf(progress, sizeof(progress), "接收中: %.0f%% (%lu/%lu 字节)",
                         percent, (unsigned long)x4im_rx_state.received_size, (unsigned long)x4im_rx_state.expected_size);
                display_draw_text_menu(20, 140, progress, COLOR_BLACK, COLOR_WHITE);
            } else {
                display_draw_text_menu(20, 140, "正在从手机请求...", COLOR_BLACK, COLOR_WHITE);
            }
        }
    }
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    // 根据工作模式处理按键
    if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
        // ==================== 传输模式按键处理 ====================
        handle_transfer_mode_button(screen, btn);
        // 传输模式需要重绘
        screen->needs_redraw = true;
    } else {
        // ==================== 阅读模式按键处理 ====================
        handle_reading_mode_button(screen, btn);
        // 阅读模式的按钮处理函数内部已经自行刷新了屏幕，不需要设置 needs_redraw
        // 这样可以避免双重刷新（一次局刷 + 一次全刷）
    }
}

/**
 * @brief 传输模式按键处理
 */
static void handle_transfer_mode_button(screen_t *screen, button_t btn)
{
    switch (btn) {
        case BTN_BACK:
            // 返回菜单
            ESP_LOGI(TAG, "Exiting transfer mode");
            
            // 如果正在传输，先取消
            if (s_ble_state.state == BLE_READER_STATE_RECEIVING) {
                ESP_LOGI(TAG, "Cancelling ongoing transfer");
                // 可选：发送取消通知给 Client
            }
            
            // 返回主菜单
            if (s_ble_state.device_connected) {
                ble_reader_screen_disconnect();
            }
            screen_manager_show("home");
            break;

        case BTN_CONFIRM:
            if (s_ble_state.state == BLE_READER_STATE_READY) {
                // 传输完成后，重置状态继续等待
                s_ble_state.state = BLE_READER_STATE_WAITING;
                s_ble_state.transfer_bytes_received = 0;
                s_ble_state.transfer_bytes_total = 0;
                ESP_LOGI(TAG, "Ready for next transfer");
            }
            break;

        default:
            // 传输模式下其他按键无效
            ESP_LOGD(TAG, "Button ignored in transfer mode: %d", btn);
            break;
    }
}

// ========== 按键处理辅助函数 ==========

/** @brief 检查是否允许翻页（需要初始化完成或无书籍） */
static inline bool can_control_paging(void) {
    return s_ble_state.current_book_id == 0 || s_ble_state.initialization_complete;
}

/** @brief 翻到上一页：使用 seek，恢复历史或回到开头 */
static inline void page_previous(void) {
    if (s_ble_state.current_page > 0) {
        s_ble_state.current_page--;
        if (s_ble_state.history_len > 0) {
            s_ble_state.char_position = s_ble_state.history_char_pos[--s_ble_state.history_len];
        } else {
            s_ble_state.char_position = 0;
        }
        ESP_LOGI(TAG, "Page previous: %u (seek=%zu)", s_ble_state.current_page, s_ble_state.char_position);
        update_cached_window(s_ble_state.current_page);
        send_position_snapshot();
        send_page_sync_notification(s_ble_state.current_page);
    }
}

/** @brief 翻到下一页：保存当前页到历史，计算新的 seek 位置 */
static inline void page_next(void) {
    bool can_next = (s_ble_state.total_pages == 0 || s_ble_state.current_page < s_ble_state.total_pages - 1);
    if (can_next) {
        // 保存当前页起始到历史栈
        if (s_ble_state.history_len < (uint8_t)(sizeof(s_ble_state.history_char_pos)/sizeof(s_ble_state.history_char_pos[0]))) {
            s_ble_state.history_char_pos[s_ble_state.history_len++] = s_ble_state.char_position;
        }
        // seek：下一页起始 = 当前起始 + 当前消耗
        s_ble_state.char_position += s_ble_state.last_page_consumed;
        s_ble_state.current_page++;
        ESP_LOGI(TAG, "Page next: %u (seek=%zu)", s_ble_state.current_page, s_ble_state.char_position);
        update_cached_window(s_ble_state.current_page);
        send_position_snapshot();
        send_page_sync_notification(s_ble_state.current_page);
    }
}

/** @brief 滚动上一屏：从历史恢复或回到开头 */
static inline void scroll_up(void) {
    if (s_ble_state.history_len > 0) {
        s_ble_state.char_position = s_ble_state.history_char_pos[--s_ble_state.history_len];
    } else if (s_ble_state.char_position > 0) {
        s_ble_state.char_position = 0;
    }
    ESP_LOGI(TAG, "Scroll UP: seek=%zu", s_ble_state.char_position);
    send_position_snapshot();
    draw_reading_mode_screen(true);
    display_refresh(REFRESH_MODE_PARTIAL);
}

/** @brief 滚动下一屏：步进 seek，使用实际消耗计算 */
static inline void scroll_down(void) {
    // 步进大小 = 当前页实际消耗的字符数（包括换行符）
    // 这是真实的、已填充到屏幕上的字符数，不需要用估算值调整
    size_t step = s_ble_state.last_page_consumed;
    if (step == 0) {
        // 如果没有有效消耗，用估算值作为最小步进
        int chars_per_screen_est = calculate_chars_per_screen();
        step = (size_t)((chars_per_screen_est > 0) ? chars_per_screen_est : 1);
    }
    
    // 尝试步进
    size_t new_pos = s_ble_state.char_position + step;
    
    // 检查是否超过当前页末尾
    if (s_ble_state.total_chars > 0 && new_pos >= s_ble_state.total_chars) {
        // 已经到或超过页末：需要翻到下一页
        bool can_next = (s_ble_state.total_pages == 0 || s_ble_state.current_page < s_ble_state.total_pages - 1);
        if (can_next) {
            // 保存当前页起始到历史，翻下一页
            if (s_ble_state.history_len < (uint8_t)(sizeof(s_ble_state.history_char_pos)/sizeof(s_ble_state.history_char_pos[0]))) {
                s_ble_state.history_char_pos[s_ble_state.history_len++] = s_ble_state.char_position;
            }
            s_ble_state.char_position = new_pos;  // seek 继续往前
            s_ble_state.current_page++;
            ESP_LOGI(TAG, "Scroll DOWN: auto-load next page %u (seek=%zu, consumed=%zu)", s_ble_state.current_page, s_ble_state.char_position, step);
            update_cached_window(s_ble_state.current_page);
            send_position_snapshot();
            send_page_sync_notification(s_ble_state.current_page);
            draw_reading_mode_screen(false);
            display_refresh(REFRESH_MODE_FULL);
        } else {
            // 已是最后一页末尾：无法再翻，留在原位
            ESP_LOGI(TAG, "Scroll DOWN: already at end (seek=%zu)", s_ble_state.char_position);
        }
    } else {
        // 仍在当前页内：只需更新 seek 并刷屏
        s_ble_state.char_position = new_pos;
        ESP_LOGI(TAG, "Scroll DOWN: seek=%zu (consumed=%zu, in page)", s_ble_state.char_position, step);
        // 保存当前页起始到历史（用于 VOLUME_UP 返回）
        if (s_ble_state.history_len < (uint8_t)(sizeof(s_ble_state.history_char_pos)/sizeof(s_ble_state.history_char_pos[0]))) {
            s_ble_state.history_char_pos[s_ble_state.history_len++] = s_ble_state.char_position - step;
        }
        send_position_snapshot();
        draw_reading_mode_screen(true);
        display_refresh(REFRESH_MODE_PARTIAL);
    }
}

/**
 * @brief 阅读模式按键处理
 */
static void handle_reading_mode_button(screen_t *screen, button_t btn)
{
    switch (btn) {
        case BTN_LEFT:
            if (can_control_paging()) {
                page_previous();
            }
            break;

        case BTN_RIGHT:
            if (can_control_paging()) {
                page_next();
            }
            break;

        case BTN_VOLUME_UP:
            if (can_control_paging()) {
                scroll_up();
            }
            break;

        case BTN_VOLUME_DOWN:
            if (can_control_paging()) {
                scroll_down();
            }
            break;

        case BTN_CONFIRM:
            // 确认键
            if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
                // 初始化确认：标记为初始化完成，开始发送初始三页给手机
                s_ble_state.initialization_complete = true;
                ESP_LOGI(TAG, "Book initialization confirmed, starting to send initial pages");

                // 初始化缓存窗口（从第0页开始）
                update_cached_window(0);

                // 发送用户确认通知，告诉手机可以开始渲染并发送第 2、3 帧
                char notify_msg[32];
                snprintf(notify_msg, sizeof(notify_msg), "USER_CONFIRMED:page_0");
                ble_manager_send_notification((const uint8_t *)notify_msg, strlen(notify_msg));
                ESP_LOGI(TAG, "Sent USER_CONFIRMED notification to Android");

                // 然后发送初始页码 0 给手机（用于缓存管理）
                send_page_sync_notification(0);
            }
            break;

        case BTN_BACK:
            // Back button
            if (s_ble_state.device_connected) {
                ble_reader_screen_disconnect();
            }
            screen_manager_show("home");
            break;

        default:
            break;
    }
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "BLE Reader screen shown");
    s_context = screen_manager_get_context();
    screen->needs_redraw = true;

    // 初始化互斥锁（必须在其他初始化之前）
    if (!init_x4im_mutex()) {
        ESP_LOGE(TAG, "Failed to initialize X4IM mutex");
        return;
    }

    // 先初始化蓝牙协议（流式写入模式，无需外部缓冲）
    if (!ble_book_protocol_init()) {
        ESP_LOGE(TAG, "Failed to initialize BLE protocol");
        deinit_x4im_mutex();
        return;
    }

    // 注册协议回调
    ble_book_protocol_register_page_ready_cb(on_page_ready);
    ble_cache_register_preload_cb(on_preload_needed);

    // 初始化蓝牙管理器（需要大块连续内存）
    if (!ble_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize BLE manager");
        ble_book_protocol_deinit();
        deinit_x4im_mutex();
        return;
    }

    // BLE初始化成功后，再分配Page buffer（避免堆碎片化）
    // 注意：如果内存碎片导致分配失败，系统会直接从文件读到帧缓冲，但不会中断BLE
    if (!init_page_buffer()) {
        ESP_LOGW(TAG, "Page buffer allocation failed due to fragmentation, will read pages directly to framebuffer");
    }

    // 初始化三槽窗口目录并重置状态
    slot_init();
    slot_prepare_window((int32_t)s_ble_state.current_page);

    // 注册蓝牙回调
    ble_manager_register_connect_cb(ble_connect_callback);
    ble_manager_register_data_received_cb(ble_data_received_callback);

    // 设备作为外设，开始广播等待手机连接
    s_ble_state.state = BLE_READER_STATE_IDLE;
    ESP_LOGI(TAG, "Waiting for phone connection (advertising as MFP-EPD)...");
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "BLE Reader screen hidden");

    // 先注销回调函数，防止在清理过程中回调被触发
    ble_manager_register_connect_cb(NULL);
    ble_manager_register_data_received_cb(NULL);

    // 清理蓝牙连接
    if (s_ble_state.device_connected) {
        ble_reader_screen_disconnect();
        
        // 等待断开连接完成（异步操作）
        // ble_gap_terminate 是异步的，需要等待断开事件完成
        ESP_LOGI(TAG, "Waiting for BLE disconnect to complete...");
        vTaskDelay(pdMS_TO_TICKS(200));  // 等待 200ms 让断开连接完成
    }

    // 在断开连接完成后再销毁 BLE 协议栈
    ble_manager_deinit();
    ble_book_protocol_deinit();

    // 释放页面缓冲区
    deinit_page_buffer();

    // 释放互斥锁
    deinit_x4im_mutex();

    s_context = NULL;
}

void ble_reader_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE reader screen");

    // 初始化屏幕结构
    g_ble_reader_screen.name = "ble_reader";
    g_ble_reader_screen.user_data = NULL;
    g_ble_reader_screen.on_show = on_show;
    g_ble_reader_screen.on_hide = on_hide;
    g_ble_reader_screen.on_draw = on_draw;
    g_ble_reader_screen.on_event = on_event;
    g_ble_reader_screen.is_visible = false;
    g_ble_reader_screen.needs_redraw = false;

    // 初始化状态
    s_ble_state.state = BLE_READER_STATE_IDLE;
    s_ble_state.device_connected = false;

    ESP_LOGI(TAG, "BLE reader screen initialized");
}

screen_t* __attribute__((unused)) ble_reader_screen_get_instance(void)
{
    if (g_ble_reader_screen.name == NULL) {
        ble_reader_screen_init();
    }
    return &g_ble_reader_screen;
}

ble_reader_state_t __attribute__((unused)) ble_reader_screen_get_state(void)
{
    return s_ble_state.state;
}

bool __attribute__((unused)) ble_reader_screen_connect_device(const uint8_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    ESP_LOGI(TAG, "Connecting to device: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    memcpy(s_ble_state.connected_device, addr, 6);
    s_ble_state.state = BLE_READER_STATE_CONNECTING;

    return ble_manager_connect(addr);
}

void __attribute__((unused)) ble_reader_screen_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from device");

    if (s_ble_state.device_connected) {
        s_ble_state.device_connected = false;
        s_ble_state.state = BLE_READER_STATE_IDLE;
        ble_manager_disconnect();
    }
}

void __attribute__((unused)) ble_reader_screen_set_current_book(uint16_t book_id)
{
    s_ble_state.current_book_id = book_id;
    s_ble_state.current_page = 0;
    s_ble_state.state = BLE_READER_STATE_READING;

    update_cached_window(s_ble_state.current_page);

    // 尝试加载第一页
    load_current_page();
}

void __attribute__((unused)) ble_reader_screen_goto_page(uint16_t page_num)
{
    if (s_ble_state.current_book_id == 0) {
        ESP_LOGW(TAG, "No book selected");
        return;
    }

    s_ble_state.current_page = page_num;
    update_cached_window(s_ble_state.current_page);
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

void __attribute__((unused)) ble_reader_screen_next_page(void)
{
    if (s_ble_state.current_book_id == 0) {
        return;
    }

    if (s_ble_state.total_pages > 0 && 
        s_ble_state.current_page >= s_ble_state.total_pages - 1) {
        return;  // 已经在最后一页
    }

    s_ble_state.current_page++;
    update_cached_window(s_ble_state.current_page);
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

void __attribute__((unused)) ble_reader_screen_prev_page(void)
{
    if (s_ble_state.current_book_id == 0 || s_ble_state.current_page == 0) {
        return;
    }

    s_ble_state.current_page--;
    update_cached_window(s_ble_state.current_page);
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

/**
 * @brief 设置 BLE 工作模式
 */
void __attribute__((unused)) ble_reader_set_mode(ble_work_mode_t mode)
{
    if (s_ble_state.work_mode == mode) {
        return; // 模式相同，无需切换
    }

    ESP_LOGI(TAG, "Switching BLE mode: %s -> %s",
             s_ble_state.work_mode == BLE_MODE_READING ? "READING" : "TRANSFER",
             mode == BLE_MODE_READING ? "READING" : "TRANSFER");

    s_ble_state.work_mode = mode;

    if (mode == BLE_MODE_TRANSFER) {
        // 进入传输模式
        s_ble_state.state = BLE_READER_STATE_WAITING;
        s_ble_state.transfer_bytes_received = 0;
        s_ble_state.transfer_bytes_total = 0;
        s_ble_state.transfer_file_count = 0;
        memset(s_ble_state.transfer_filename, 0, sizeof(s_ble_state.transfer_filename));
        
        ESP_LOGI(TAG, "Entered TRANSFER mode");
    } else {
        // 进入阅读模式
        s_ble_state.state = BLE_READER_STATE_IDLE;
        
        ESP_LOGI(TAG, "Entered READING mode");
    }

    // 触发重绘
    g_ble_reader_screen.needs_redraw = true;
}

/**
 * @brief 获取当前 BLE 工作模式
 */
ble_work_mode_t __attribute__((unused)) ble_reader_get_mode(void)
{
    return s_ble_state.work_mode;
}
