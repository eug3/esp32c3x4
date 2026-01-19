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
 * VFS 三页缓存协议:
 *   - 文件名格式: page_{logicalIndex}
 *   - VFS页面映射: page_id = abs(logical_index) % 3
 *   - 窗口范围: [current-1, current, current+1]
 */

#include "ble_reader_screen.h"
#include "ble_manager.h"
// #include "ble_book_protocol.h"  // 已废弃，统一使用VFS章节协议
#include "../vfs/vfs_reader.h"    // VFS虚拟文件系统
#include "display_engine.h"
#include "fonts.h"
#include "xt_eink_font_impl.h"
#include "../fs_utils.h"     // 共享文件系统工具
#include "../vfs/vfs_reader.h" // VFS虚拟文件系统
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

// 全局变量：跟踪是否是新的文件传输（用于决定创建新文件还是追加）
bool g_ble_new_transfer = true;

#define BLE_TEXT_MAX_BYTES 4096  // 减小缓冲区以节省内存 (原8192太大导致内存碎片)
 

// 内容区域布局常量（显示、翻页、消耗计算统一使用）
#define BLE_CONTENT_Y_START 30      // 内容区域起始 Y（页码信息下方）
#define BLE_CONTENT_BOTTOM_MARGIN 10 // 底部留白
#define BLE_CONTENT_X_MARGIN 10      // 左右边距

// ========== 固定 3 页滑动窗口（-1, 0, +1）用于BLE接收管理 ==========
#define BLE_VFS_CACHE_DIR   "/littlefs/ble_vfs"
#define BLE_VFS_PAGE_COUNT 3

typedef struct {
    int32_t logical_index;   // 对应的逻辑页索引，可为负
    bool ready;              // 文件是否已写完可读
    char path[64];           // VFS页面文件路径（page0/1/2）
} ble_vfs_page_entry_t;

static ble_vfs_page_entry_t s_vfs_pages[BLE_VFS_PAGE_COUNT];

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
#define X4IM_CMD_LIST_CHAPTERS     0x99    // ESP32→Client 请求章节列表
#define X4IM_CMD_CHAPTER_INFO      0x9A    // Client→ESP32 章节信息（JSON）
#define X4IM_CMD_SELECT_CHAPTER    0x9B    // ESP32→Client 选择章节
#define X4IM_CMD_CHAPTER_NAVIGATE  0x9C    // ESP32→Client 请求切换到相邻章节（next/prev）

// 章节导航方向
#define CHAPTER_NAV_PREVIOUS       0x00    // 请求上一章
#define CHAPTER_NAV_NEXT           0x01    // 请求下一章

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
    
    // ========== VFS集成 ==========
    vfs_file_t *vfs_book;              // VFS虚拟文件对象
    
    // ========== 章节浏览 ==========
    bool chapter_browser_active;       // 是否处于章节选择模式
    int chapter_list_count;            // 可用章节总数
    int chapter_list_selection;        // 当前选择的章节索引
    char book_title[128];              // 书名
    struct {
        int index;                     // 章节索引
        char title[128];               // 章节标题
        int page_count;                // 页数
        bool available;                // 是否可读（已下载）
    } chapter_list[64];                // 最多64章
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
    .chapter_browser_active = false,
    .chapter_list_count = 0,
    .chapter_list_selection = 0,
    .book_title = {0},
};

// 章节信息接收状态（支持分包JSON）
typedef struct {
    bool receiving;
    size_t expected;
    size_t received;
    char *buf;
} chapter_info_rx_t;
static chapter_info_rx_t s_chinfo_rx = {0};

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

// VFS 页面管理（用于BLE接收）
static bool ensure_vfs_cache_dir(void);
static void vfs_page_init(void);
static int vfs_page_id_from_index(int32_t idx);
static void vfs_prepare_window(int32_t center_index);
static bool vfs_get_path_if_ready(int32_t logical_index, char *out_path, size_t out_size);
static void vfs_mark_page_ready(int page_id, int32_t logical_index);
static bool load_current_page(void);
static bool init_page_buffer(void);
static void deinit_page_buffer(void);

// 小窗口槽位管理
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

// 章节浏览
static void request_chapter_list(void);
static void parse_chapter_info_json(const char *json_data, size_t json_len);
static void enter_chapter_browser(void);
static void exit_chapter_browser(void);
static void draw_chapter_browser_screen(void);
static void handle_chapter_browser_button(screen_t *screen, button_t btn);
static void select_and_load_chapter(int chapter_index);

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

        // 使用普通heap，不占用DMA内存
        s_page_buffer = (uint8_t *)heap_caps_malloc(PAGE_BUFFER_SIZE, MALLOC_CAP_8BIT);
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

static bool ensure_vfs_cache_dir(void)
{
    struct stat st;
    if (stat(BLE_VFS_CACHE_DIR, &st) != 0) {
        if (mkdir(BLE_VFS_CACHE_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create VFS cache dir: %s", BLE_VFS_CACHE_DIR);
            return false;
        }
    }
    return true;
}

static int vfs_page_id_from_index(int32_t idx)
{
    int m = idx % BLE_VFS_PAGE_COUNT;
    if (m < 0) {
        m += BLE_VFS_PAGE_COUNT;
    }
    return m;
}

static void vfs_page_init(void)
{
    if (!ensure_vfs_cache_dir()) {
        return;
    }

    for (int i = 0; i < BLE_VFS_PAGE_COUNT; i++) {
        s_vfs_pages[i].logical_index = INT32_MIN;
        s_vfs_pages[i].ready = false;
        // 章节管理模式：不再使用按页缓存文件，路径留空
        s_vfs_pages[i].path[0] = '\0';
    }
}

static void vfs_prepare_window(int32_t center_index)
{
    if (!ensure_vfs_cache_dir()) {
        return;
    }

    int32_t targets[BLE_VFS_PAGE_COUNT] = {center_index - 1, center_index, center_index + 1};
    for (int i = 0; i < BLE_VFS_PAGE_COUNT; i++) {
        int page_id = vfs_page_id_from_index(targets[i]);
        ble_vfs_page_entry_t *page = &s_vfs_pages[page_id];

        if (page->logical_index != targets[i]) {
            // 该VFS页面要被复用，删除旧文件并标记 pending
            unlink(page->path);
            page->logical_index = targets[i];
            page->ready = false;
        }
    }
}

static bool vfs_get_path_if_ready(int32_t logical_index, char *out_path, size_t out_size)
{
    int page_id = vfs_page_id_from_index(logical_index);
    ble_vfs_page_entry_t *page = &s_vfs_pages[page_id];

    if (page->logical_index != logical_index || !page->ready) {
        return false;
    }

    if (out_path != NULL && out_size > 0) {
        snprintf(out_path, out_size, "%s", page->path);
    }
    return true;
}

static void vfs_mark_page_ready(int page_id, int32_t logical_index)
{
    if (page_id < 0 || page_id >= BLE_VFS_PAGE_COUNT) {
        return;
    }

    s_vfs_pages[page_id].logical_index = logical_index;
    s_vfs_pages[page_id].ready = true;
}

/**
 * @brief 标记页面已加载（VFS版本 - 由 vfs_read 自动处理）
 */
static bool load_current_page(void)
{
    // 此函数已弃用 - VFS API会自动处理缓存
    // 保留此函数以维持兼容性
    s_ble_state.page_loaded = (s_ble_state.vfs_book != NULL);
    return s_ble_state.page_loaded;
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
static size_t __attribute__((unused)) count_utf8_chars(const char *str)
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
 * @brief 通用文本处理：绘制或测量消耗字节数
 * 算法：计算行列布局，逐字填充行缓冲，屏幕满或文件尾则返回消耗的字节数
 * 
 * 关键规则：
 * - 遇到换行符后，该行剩余空间不消耗字符，直接换行
 * - 返回值是消耗的字节数（用于 seek），不是字符数
 * 
 * @param text 要处理的文本（已经是从文件当前位置读取的内容）
 * @param do_draw true=绘制到屏幕，false=只计算消耗
 * @return 消耗的字节数
 */
static size_t process_wrapped_text(int x, int y, int max_width, int max_height,
                                   const char *text, size_t char_offset, bool do_draw)
{
    (void)char_offset;  // 不再使用 char_offset 参数，保留接口兼容性
    
    if (!text || !*text) return 0;

    sFONT *font;
    int line_height;
    get_text_layout_params(&font, &line_height);
    if (!font) return 0;

    // 计算布局：能显示多少行
    int lines_can_show = max_height / line_height;
    if (lines_can_show <= 0) lines_can_show = 1;

    char line[256];
    int line_len = 0;
    int lines_filled = 0;
    int cur_y = y;
    
    const char *p = text;           // 当前处理位置
    const char *start = text;       // 起始位置，用于计算消耗字节数

    // 逐字填充，直到屏幕满或文件结束
    while (*p && lines_filled < lines_can_show) {
        // 换行符：直接换行，消耗这个换行符
        if (*p == '\n') {
            if (do_draw && line_len > 0) {
                line[line_len] = '\0';
                display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
            }
            line_len = 0;
            cur_y += line_height;
            lines_filled++;
            p++;  // 消耗换行符（1字节）
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
            // 不移动 p，下次循环重新处理这个字符
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
        p += clen;
    }

    // 输出最后一行（如果还有空间且有内容）
    if (do_draw && line_len > 0 && lines_filled < lines_can_show) {
        line[line_len] = '\0';
        display_draw_text_font(x, cur_y, line, font, COLOR_BLACK, COLOR_WHITE);
    }

    // 返回消耗的字节数
    return (size_t)(p - start);
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
    int page_id;                // 当前写入的VFS页面 ID
    bool use_sd_card;           // 是否写入SD卡（传输模式）
    uint16_t flags;             // X4IM v2 flags
} x4im_rx_state = {.logical_index = -1, .page_id = -1};

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
    x4im_rx_state.page_id = -1;
    
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
static void __attribute__((unused)) scan_directory_recursive(const char *path, int *file_count, int *dir_count, uint64_t *used_size)
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
        char *full_path = heap_caps_malloc(512, MALLOC_CAP_8BIT);
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
 * @brief 蓝牙数据接收回调 - 支持 VFS 章节协议（TXT文本数据）
 */
static void ble_data_received_callback(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        ESP_LOGW(TAG, "Received NULL or empty data");
        if (data != NULL) {
            free((void *)data);
        }
        return;
    }

    // 检查屏幕是否仍然激活
    screen_t *current_screen = screen_manager_get_current();
    if (current_screen == NULL || current_screen != &g_ble_reader_screen) {
        ESP_LOGW(TAG, "BLE data received but screen is not active, discarding %u bytes", length);
        free((void *)data);
        return;
    }

    ESP_LOGI(TAG, "===== BLE TXT DATA RECEIVED: %u bytes =====", length);
    
    // 检查是否为命令包（保留兼容性，但VFS协议主要处理文本数据）
    bool is_command = (length >= 3 && data[0] == 0xA5 && data[1] == 0x5A);
    
    if (is_command) {
        uint8_t cmd = data[2];
        // 处理章节信息JSON（Client→ESP32）
        if (cmd == X4IM_CMD_CHAPTER_INFO) {
            // 头格式: [A5,5A,9A,total_chapters,len_low,len_high][JSON...]
            if (length >= 6) {
                uint8_t total_ch = data[3];
                size_t json_len = (size_t)(data[4] | (data[5] << 8));
                size_t header_json_bytes = (length > 6) ? (size_t)(length - 6) : 0;
                ESP_LOGI(TAG, "Received CHAPTER_INFO header: total=%u, json_len=%zu, first_chunk=%zu", total_ch, json_len, header_json_bytes);

                // 初始化接收缓冲
                if (json_len == 0) {
                    // 空列表
                    s_ble_state.chapter_list_count = 0;
                    s_ble_state.book_title[0] = '\0';
                    // 刷新显示
                    if (s_ble_state.chapter_browser_active) {
                        draw_chapter_browser_screen();
                        display_refresh(REFRESH_MODE_FULL);
                    }
                } else {
                    // 分配缓冲并复制头包中携带的JSON数据
                    if (s_chinfo_rx.buf) { free(s_chinfo_rx.buf); s_chinfo_rx.buf = NULL; }
                    s_chinfo_rx.buf = (char *)malloc(json_len + 1);
                    if (!s_chinfo_rx.buf) {
                        ESP_LOGE(TAG, "Failed to alloc chapter JSON buffer (%zu)", json_len);
                        free((void *)data);
                        return;
                    }
                    s_chinfo_rx.expected = json_len;
                    s_chinfo_rx.received = 0;
                    s_chinfo_rx.receiving = true;
                    if (header_json_bytes > 0) {
                        memcpy(s_chinfo_rx.buf, data + 6, header_json_bytes);
                        s_chinfo_rx.received = header_json_bytes;
                    }

                    // 如果已全部收到，直接解析
                    if (s_chinfo_rx.received >= s_chinfo_rx.expected) {
                        s_chinfo_rx.buf[s_chinfo_rx.expected] = '\0';
                        parse_chapter_info_json(s_chinfo_rx.buf, s_chinfo_rx.expected);
                        s_chinfo_rx.receiving = false;
                        free(s_chinfo_rx.buf); s_chinfo_rx.buf = NULL;
                        if (s_ble_state.chapter_browser_active) {
                            draw_chapter_browser_screen();
                            display_refresh(REFRESH_MODE_FULL);
                        }
                    }
                }
            }
            free((void *)data);
            return;
        }
        // 其它命令保持原兼容行为：忽略
        ESP_LOGW(TAG, "Received command 0x%02X, ignored by VFS TXT flow", cmd);
        free((void *)data);
        return;
    }
    
    // 如果正在接收章节JSON的后续分块，则累积并处理
    if (s_chinfo_rx.receiving) {
        if (length > 0 && s_chinfo_rx.buf && s_chinfo_rx.received < s_chinfo_rx.expected) {
            size_t copy_len = length;
            if (s_chinfo_rx.received + copy_len > s_chinfo_rx.expected) {
                copy_len = s_chinfo_rx.expected - s_chinfo_rx.received;
            }
            memcpy(s_chinfo_rx.buf + s_chinfo_rx.received, data, copy_len);
            s_chinfo_rx.received += copy_len;
            ESP_LOGI(TAG, "Accumulating CHAPTER_INFO JSON: %zu/%zu", s_chinfo_rx.received, s_chinfo_rx.expected);
            if (s_chinfo_rx.received >= s_chinfo_rx.expected) {
                s_chinfo_rx.buf[s_chinfo_rx.expected] = '\0';
                parse_chapter_info_json(s_chinfo_rx.buf, s_chinfo_rx.expected);
                s_chinfo_rx.receiving = false;
                free(s_chinfo_rx.buf); s_chinfo_rx.buf = NULL;
                if (s_ble_state.chapter_browser_active) {
                    draw_chapter_browser_screen();
                    display_refresh(REFRESH_MODE_FULL);
                }
            }
        }
        free((void *)data);
        return;
    }

    // ========== 检测并解析 X4IM v2 协议头 ==========
    // X4IM v2 头部: "X4IM" (4B) + version(1B) + type(1B) + flags(2B) + payload_size(4B) + ...
    const uint8_t *payload_data = data;
    size_t payload_length = length;
    bool has_x4im_header = false;
    
    if (length >= X4IM_HEADER_SIZE && 
        data[0] == 'X' && data[1] == '4' && data[2] == 'I' && data[3] == 'M' && data[4] == 0x02) {
        // 解析 X4IM v2 头部
        uint8_t type = data[5];
        uint16_t flags = data[6] | (data[7] << 8);
        uint32_t declared_payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
        
        ESP_LOGI(TAG, "X4IM v2 header detected: flags=0x%04X, payload=%lu bytes",
                 flags, (unsigned long)declared_payload_size);
        
        // 跳过32字节头部，只处理payload
        payload_data = data + X4IM_HEADER_SIZE;
        payload_length = (length > X4IM_HEADER_SIZE) ? (length - X4IM_HEADER_SIZE) : 0;
        has_x4im_header = true;
    }

    // 检测EOF标记
    bool is_eof_marker = false;
    if (payload_length >= 4) {
        if ((payload_length == 5 && payload_data[0] == 0x00 && payload_data[1] == 'E' && payload_data[2] == 'O' && payload_data[3] == 'F' && payload_data[4] == '\n') ||
            (payload_length == 4 && payload_data[0] == 0x00 && payload_data[1] == 'E' && payload_data[2] == 'O' && payload_data[3] == 'F')) {
            is_eof_marker = true;
            ESP_LOGI(TAG, "*** Received EOF marker - transfer complete! ***");
        }
    }
    
    // 写入当前章节的缓存文件
    if (s_ble_state.vfs_book != NULL && payload_length > 0) {
        int current_chapter = vfs_get_current_chapter(s_ble_state.vfs_book);
        
        char chapter_path[128];
        snprintf(chapter_path, sizeof(chapter_path), "/littlefs/ble_vfs/current_ch%d.txt", 
                 current_chapter);
        
        ESP_LOGI(TAG, "Write path: %s", chapter_path);
        
        struct stat st;
        if (stat("/littlefs/ble_vfs", &st) != 0) {
            mkdir("/littlefs/ble_vfs", 0755);
        }
        
        // ========== EOF标记处理：触发显示 ==========
        if (is_eof_marker) {
            ESP_LOGI(TAG, "=== Transfer complete for chapter %d ===", current_chapter);
            
            // 1. 设置状态
            s_ble_state.page_loaded = true;
            s_ble_state.state = BLE_READER_STATE_READING;
            
            // 2. 初始化 book_id（如果尚未初始化）
            if (s_ble_state.current_book_id == 0) {
                s_ble_state.current_book_id = 1;
                ESP_LOGI(TAG, "Book ID initialized to 1");
            }
            
            // 3. 标记初始化完成（跳过确认提示，直接显示内容）
            if (!s_ble_state.initialization_complete) {
                s_ble_state.initialization_complete = true;
                s_ble_state.showing_confirm_prompt = false;
                ESP_LOGI(TAG, "Auto-confirmed: initialization_complete = true");
            }
            
            // 4. 重置读取位置到文件开头
            s_ble_state.char_position = 0;
            s_ble_state.current_page = 0;
            s_ble_state.history_len = 0;
            s_ble_state.last_page_consumed = 0;
            
            // 5. 获取文件大小
            struct stat file_st;
            if (stat(chapter_path, &file_st) == 0) {
                s_ble_state.total_chars = (size_t)file_st.st_size;
                ESP_LOGI(TAG, "File ready: %s (%zu bytes)", chapter_path, s_ble_state.total_chars);
            } else {
                s_ble_state.total_chars = 0;
                ESP_LOGW(TAG, "Cannot stat file: %s", chapter_path);
            }
            
            // 6. 重置 VFS 文件位置到开头
            if (s_ble_state.vfs_book != NULL) {
                vfs_seek(s_ble_state.vfs_book, 0, SEEK_SET);
                ESP_LOGI(TAG, "VFS seek to position 0");
            }
            
            // 7. 标记下次收到数据是新传输
            g_ble_new_transfer = true;
            
            // 8. ========== 关键：先全屏清空，再绘制和刷新 ==========
            ESP_LOGI(TAG, ">>> Clearing screen and rendering content <<<");
            display_clear(COLOR_WHITE);  // 全屏清空（白色背景）
            
            // 延迟一下确保清屏完成
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // 绘制新内容
            draw_reading_mode_screen(false);
            
            // 全屏刷新
            display_refresh(REFRESH_MODE_FULL);
            
            ESP_LOGI(TAG, "Screen refreshed successfully");
            
            free((void *)data);
            return;
        }
        
        // 正常数据：写入文件（不刷新屏幕）
        // 使用全局变量跟踪：如果是新传输则新建文件，否则追加
        const char *mode = g_ble_new_transfer ? "wb" : "ab";  // 新传输用wb，否则追加
        FILE *fp = fopen(chapter_path, mode);
        if (fp != NULL) {
            // 写入payload数据（如果有X4IM头则已跳过，否则是原始数据）
            size_t written = fwrite(payload_data, 1, payload_length, fp);
            fclose(fp);
            
            if (written == payload_length) {
                if (g_ble_new_transfer) {
                    ESP_LOGI(TAG, "New file created, wrote %zu bytes to chapter %d%s", 
                             written, current_chapter, has_x4im_header ? " (X4IM)" : "");
                    g_ble_new_transfer = false;  // 后续数据追加
                } else {
                    ESP_LOGD(TAG, "Appended %zu bytes to chapter %d%s", 
                             written, current_chapter, has_x4im_header ? " (X4IM)" : "");
                }
            } else {
                ESP_LOGE(TAG, "Write failed: expected %zu, written %zu", payload_length, written);
            }
        } else {
            ESP_LOGE(TAG, "Failed to open chapter file for writing: %s (mode=%s)", chapter_path, mode);
        }
    } else if (payload_length == 0 && !is_eof_marker) {
        ESP_LOGW(TAG, "Empty payload received (length=%u, has_x4im=%d)", length, has_x4im_header);
    }
    
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
            x4im_rx_state.page_id = -1;
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
                if (!ensure_vfs_cache_dir()) {
                    xSemaphoreGive(x4im_rx_mutex);
                    free((void *)data);
                    return;
                }

                int page_id = vfs_page_id_from_index(incoming_index);
                x4im_rx_state.page_id = page_id;
                x4im_rx_state.logical_index = incoming_index;
                // 章节管理模式：接收文件名采用 VFS 的书籍哈希与当前章节
                uint32_t vfs_book_hash = vfs_get_book_hash(s_ble_state.vfs_book);
                int current_chapter = vfs_get_current_chapter(s_ble_state.vfs_book);
                // 清理旧章节文件，确保写入干净
                snprintf(x4im_rx_state.filename, sizeof(x4im_rx_state.filename),
                         "%s/0x%08" PRIx32 "_ch%d.txt",
                         BLE_VFS_CACHE_DIR,
                         vfs_book_hash,
                         current_chapter);
                unlink(x4im_rx_state.filename);
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

                if (!x4im_rx_state.use_sd_card && x4im_rx_state.page_id >= 0) {
                    vfs_mark_page_ready(x4im_rx_state.page_id, x4im_rx_state.logical_index);
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

                if (!x4im_rx_state.use_sd_card && x4im_rx_state.page_id >= 0) {
                    vfs_mark_page_ready(x4im_rx_state.page_id, x4im_rx_state.logical_index);
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
                        // 获取文件大小（字节数）用于翻页边界检测
                        char filename[64];
                        snprintf(filename, sizeof(filename), "%s", x4im_rx_state.filename);
                        struct stat st;
                        if (stat(filename, &st) == 0) {
                            s_ble_state.total_chars = (size_t)st.st_size;  // 存储文件大小（字节数）
                            ESP_LOGI(TAG, "Page loaded: file size = %zu bytes", s_ble_state.total_chars);
                        } else {
                            s_ble_state.total_chars = 0;
                            ESP_LOGW(TAG, "Failed to get file size");
                        }
                    }

                    // 标记页面已加载（仅对当前页）
                    s_ble_state.page_loaded = (x4im_rx_state.logical_index == (int32_t)s_ble_state.current_page);
                }

                // 触发重绘
                screen_t *screen = screen_manager_get_current();
                if (screen != NULL && screen == &g_ble_reader_screen) {
                    screen->needs_redraw = true;
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
    // Construct page notification message: PAGE:book:chapter
    char msg[64];
    int current_chapter = vfs_get_current_chapter(s_ble_state.vfs_book);
    if (current_chapter < 0) current_chapter = 0;
    
    snprintf(msg, sizeof(msg), "PAGE:0x%08lX:%d", 
             (unsigned long)s_ble_state.current_book_hash, current_chapter);

    ESP_LOGI(TAG, "Sending chapter request: %s (book=0x%08lX, chapter=%d)", 
             msg, (unsigned long)s_ble_state.current_book_hash, current_chapter);

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
 * @brief 发送章节导航请求到 Client（请求上一章/下一章）
 * 格式：[Magic(2B), CMD(1B), Direction(1B)]
 * Client 收到后会调用服务器获取新章节 URL，然后发送新章节内容
 * @param direction CHAPTER_NAV_PREVIOUS=上一章, CHAPTER_NAV_NEXT=下一章
 */
static void send_chapter_navigate_request(uint8_t direction)
{
    uint8_t cmd[4] = {
        0xA5, 0x5A,                    // Magic
        X4IM_CMD_CHAPTER_NAVIGATE,     // 0x9C
        direction
    };
    
    bool sent = ble_manager_send_notification(cmd, sizeof(cmd));
    ESP_LOGI(TAG, "[CHAPTER_NAVIGATE] direction=%s - %s",
             direction == CHAPTER_NAV_NEXT ? "NEXT" : "PREVIOUS",
             sent ? "SENT" : "FAILED");
    
    // 显示加载提示
    if (sent) {
        display_draw_text_menu(20, 100, 
                               direction == CHAPTER_NAV_NEXT ? "正在加载下一章..." : "正在加载上一章...", 
                               COLOR_BLACK, COLOR_WHITE);
        display_refresh(REFRESH_MODE_PARTIAL);
    }
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

    // 重建 3 页 VFS 窗口映射：-1,0,+1 对应 page0/1/2（取模）
    vfs_prepare_window((int32_t)current_page);

    bool ready_prev = vfs_get_path_if_ready((int32_t)s_ble_state.cached_pages[0], NULL, 0);
    bool ready_curr = vfs_get_path_if_ready((int32_t)s_ble_state.cached_pages[1], NULL, 0);
    bool ready_next = vfs_get_path_if_ready((int32_t)s_ble_state.cached_pages[2], NULL, 0);

    s_ble_state.page_loaded = ready_curr;

    ESP_LOGI(TAG, "VFS cache window -> prev:%u(%s) curr:%u(%s) next:%u(%s)",
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
 * @brief 预加载需要回调 (VFS章节协议)
 * VFS系统会自动处理章节预加载，这里仅用于记录日志
 */
static void on_preload_needed(uint16_t book_id, uint16_t chapter_index, uint8_t chapter_count)
{
    ESP_LOGI(TAG, "VFS chapter preload triggered: book=%04x, chapters=%u-%u",
             book_id, chapter_index, chapter_index + chapter_count - 1);
    
    // VFS系统会自动调用request_chapter_from_client进行预加载
    // 这里不需要手动发送BLE请求
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

        // 绘制页面内容（使用VFS API透明读取）
        char *page_buffer = heap_caps_malloc(BLE_TEXT_MAX_BYTES, MALLOC_CAP_8BIT);
        bool vfs_page_ready = false;
        int bytes_read = 0;
        
        if (!page_buffer) {
            // 内存分配失败
            ESP_LOGE(TAG, "Failed to allocate page_buffer");
            display_draw_text_menu(20, 100, "内存不足", COLOR_BLACK, COLOR_WHITE);
        } else if (s_ble_state.vfs_book == NULL) {
            // VFS未初始化
            display_draw_text_menu(20, 100, "VFS未初始化", COLOR_BLACK, COLOR_WHITE);
            free(page_buffer);
        } else {
            // 使用互斥锁防止与 BLE 接收冲突
            if (x4im_rx_mutex != NULL && xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 先seek到当前字符位置，确保从正确位置读取
                vfs_seek(s_ble_state.vfs_book, s_ble_state.char_position, SEEK_SET);
                
                // VFS已处理缓存，直接读取当前位置的数据
                bytes_read = vfs_read(s_ble_state.vfs_book, page_buffer, BLE_TEXT_MAX_BYTES - 1);
                vfs_page_ready = (bytes_read > 0);
            
                if (vfs_page_ready) {
                    page_buffer[bytes_read] = '\0';

                    // 内容区域：使用统一的布局常量
                    int content_width = SCREEN_WIDTH - 2 * BLE_CONTENT_X_MARGIN;
                    int content_height = SCREEN_HEIGHT - BLE_CONTENT_Y_START - BLE_CONTENT_BOTTOM_MARGIN;
                    // 先计算本页消耗的字节数（用于精确翻页seek）
                    // 注意：page_buffer 已经是从 char_position 位置读取的，所以 offset 传 0
                    s_ble_state.last_page_consumed = measure_wrapped_consumed(content_width, content_height, page_buffer, 0);
                    draw_wrapped_text(BLE_CONTENT_X_MARGIN, BLE_CONTENT_Y_START, content_width, content_height, page_buffer, 0);
                } else {
                    // 文件为空，显示提示
                    display_draw_text_menu(20, 100, "文件为空", COLOR_BLACK, COLOR_WHITE);
                }
                xSemaphoreGive(x4im_rx_mutex);
            } else {
                // 获取互斥锁超时或失败，显示等待提示
                display_draw_text_menu(20, 100, "正在加载...", COLOR_BLACK, COLOR_WHITE);
            }

            if (!vfs_page_ready) {
                // 本地文件读取失败，可能是文件为空或还未传输
                // 检查文件是否存在
                char chapter_path[128];
                int current_chapter = vfs_get_current_chapter(s_ble_state.vfs_book);
                snprintf(chapter_path, sizeof(chapter_path), "/littlefs/ble_vfs/current_ch%d.txt", 
                         current_chapter >= 0 ? current_chapter : 0);
                
                struct stat st;
                if (stat(chapter_path, &st) == 0 && st.st_size > 0) {
                    // 文件存在且有内容，但读取位置可能超出文件
                    display_draw_text_menu(20, 100, "已到达末尾", COLOR_BLACK, COLOR_WHITE);
                    char info[64];
                    snprintf(info, sizeof(info), "文件大小: %ld 字节", (long)st.st_size);
                    display_draw_text_menu(20, 140, info, COLOR_BLACK, COLOR_WHITE);
                } else if (x4im_rx_state.receiving) {
                    // 正在接收数据
                    display_draw_text_menu(20, 100, "正在接收...", COLOR_BLACK, COLOR_WHITE);
                    if (x4im_rx_state.expected_size > 0) {
                        char progress[64];
                        float percent = (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size;
                        snprintf(progress, sizeof(progress), "进度: %.0f%% (%lu/%lu)",
                                 percent, (unsigned long)x4im_rx_state.received_size, (unsigned long)x4im_rx_state.expected_size);
                        display_draw_text_menu(20, 140, progress, COLOR_BLACK, COLOR_WHITE);
                    }
                } else {
                    // 文件不存在或为空，等待传输
                    display_draw_text_menu(20, 100, "等待传输...", COLOR_BLACK, COLOR_WHITE);
                    display_draw_text_menu(20, 140, "请从手机发送内容", COLOR_BLACK, COLOR_WHITE);
                }
            }
            
            free(page_buffer);
        }
    }
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    // 长按CONFIRM键进入章节浏览模式
    if (event == BTN_EVENT_LONG_PRESSED && btn == BTN_CONFIRM) {
        if (s_ble_state.vfs_book != NULL && s_ble_state.device_connected && 
            s_ble_state.work_mode == BLE_MODE_READING) {
            enter_chapter_browser();
            return;
        }
    }
    
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    // 章节浏览模式优先处理
    if (s_ble_state.chapter_browser_active) {
        handle_chapter_browser_button(screen, btn);
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
                s_ble_state.transfer_file_count = 0;
                memset(s_ble_state.transfer_filename, 0, sizeof(s_ble_state.transfer_filename));
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
        
        // 重置VFS文件位置到char_position，以便重新读取
        if (s_ble_state.vfs_book != NULL) {
            vfs_seek(s_ble_state.vfs_book, s_ble_state.char_position, SEEK_SET);
        }
        
        update_cached_window(s_ble_state.current_page);
        send_position_snapshot();
        // 本地翻页不需要向手机请求，直接重绘即可
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
        
        // 重置VFS文件位置到char_position，以便重新读取
        if (s_ble_state.vfs_book != NULL) {
            vfs_seek(s_ble_state.vfs_book, s_ble_state.char_position, SEEK_SET);
        }
        
        update_cached_window(s_ble_state.current_page);
        send_position_snapshot();
        // 本地翻页不需要向手机请求，直接重绘即可
    }
}

/** @brief 滚动上一屏：从历史恢复或回到开头，到达开头时请求上一章 */
static inline void scroll_up(void) {
    if (s_ble_state.history_len > 0) {
        // 有历史记录，恢复到上一个位置
        s_ble_state.char_position = s_ble_state.history_char_pos[--s_ble_state.history_len];
        ESP_LOGI(TAG, "Scroll UP: restore to seek=%zu", s_ble_state.char_position);
        
        // 重置VFS文件位置
        if (s_ble_state.vfs_book != NULL) {
            vfs_seek(s_ble_state.vfs_book, s_ble_state.char_position, SEEK_SET);
        }
        
        send_position_snapshot();
        draw_reading_mode_screen(true);
        display_refresh(REFRESH_MODE_PARTIAL);
    } else if (s_ble_state.char_position > 0) {
        // 没有历史但不在开头，回到开头
        s_ble_state.char_position = 0;
        ESP_LOGI(TAG, "Scroll UP: back to start");
        
        // 重置VFS文件位置
        if (s_ble_state.vfs_book != NULL) {
            vfs_seek(s_ble_state.vfs_book, 0, SEEK_SET);
        }
        
        send_position_snapshot();
        draw_reading_mode_screen(true);
        display_refresh(REFRESH_MODE_PARTIAL);
    } else {
        // 已经在开头（char_position == 0 且无历史）→ 请求上一章
        ESP_LOGI(TAG, "Scroll UP: at beginning, requesting previous chapter");
        send_chapter_navigate_request(CHAPTER_NAV_PREVIOUS);
    }
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
        // 已经到达文件末尾 → 请求下一章
        ESP_LOGI(TAG, "Scroll DOWN: at end (pos=%zu, total=%zu), requesting next chapter", 
                 new_pos, s_ble_state.total_chars);
        send_chapter_navigate_request(CHAPTER_NAV_NEXT);
    } else if (s_ble_state.total_chars == 0 && s_ble_state.last_page_consumed == 0) {
        // 文件为空或未加载 → 也请求下一章
        ESP_LOGI(TAG, "Scroll DOWN: empty content, requesting next chapter");
        send_chapter_navigate_request(CHAPTER_NAV_NEXT);
    } else {
        // 仍在当前页内：只需更新 seek 并刷屏
        s_ble_state.char_position = new_pos;
        ESP_LOGI(TAG, "Scroll DOWN: seek=%zu (consumed=%zu, in page)", s_ble_state.char_position, step);
        
        // 重置VFS文件位置到char_position，以便重新读取
        if (s_ble_state.vfs_book != NULL) {
            vfs_seek(s_ble_state.vfs_book, s_ble_state.char_position, SEEK_SET);
        }
        
        // 保存当前页起始到历史（用于 VOLUME_UP 返回）
        if (s_ble_state.history_len < (uint8_t)(sizeof(s_ble_state.history_char_pos)/sizeof(s_ble_state.history_char_pos[0]))) {
            s_ble_state.history_char_pos[s_ble_state.history_len++] = s_ble_state.char_position - step;
        }
        send_position_snapshot();
        draw_reading_mode_screen(true);
        display_refresh(REFRESH_MODE_PARTIAL);
    }
}

/** @brief 切换到上一章 */
static inline void chapter_previous(void) {
    if (s_ble_state.vfs_book != NULL) {
        int current_ch = vfs_get_current_chapter(s_ble_state.vfs_book);
        if (current_ch > 0) {
            if (vfs_switch_chapter(s_ble_state.vfs_book, current_ch - 1)) {
                // 重置页面位置到新章节开头
                s_ble_state.current_page = 0;
                s_ble_state.char_position = 0;
                s_ble_state.history_len = 0;
                
                ESP_LOGI(TAG, "Chapter previous: %d -> %d", current_ch, current_ch - 1);
                update_cached_window(0);
                send_position_snapshot();
                send_page_sync_notification(0);
                
                // 刷新屏幕显示新章节
                draw_reading_mode_screen(false);
                display_refresh(REFRESH_MODE_FULL);
            }
        } else {
            ESP_LOGW(TAG, "Already at first chapter");
        }
    }
}

/** @brief 切换到下一章 */
static inline void chapter_next(void) {
    if (s_ble_state.vfs_book != NULL) {
        int current_ch = vfs_get_current_chapter(s_ble_state.vfs_book);
        int total_ch = vfs_get_total_chapters(s_ble_state.vfs_book);
        
        if (total_ch > 0 && current_ch < total_ch - 1) {
            if (vfs_switch_chapter(s_ble_state.vfs_book, current_ch + 1)) {
                // 重置页面位置到新章节开头
                s_ble_state.current_page = 0;
                s_ble_state.char_position = 0;
                s_ble_state.history_len = 0;
                
                ESP_LOGI(TAG, "Chapter next: %d -> %d", current_ch, current_ch + 1);
                update_cached_window(0);
                send_position_snapshot();
                send_page_sync_notification(0);
                
                // 刷新屏幕显示新章节
                draw_reading_mode_screen(false);
                display_refresh(REFRESH_MODE_FULL);
            }
        } else {
            ESP_LOGW(TAG, "Already at last chapter or total_chapters unknown");
        }
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
            // 音量键短按：滚动/翻页
            // 长按：进入章节浏览
            if (can_control_paging()) {
                scroll_up();
            }
            break;

        case BTN_VOLUME_DOWN:
            // 音量键短按：滚动/翻页  
            // 长按：章节切换
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
            // 其他按键：可用于章节导航
            ESP_LOGD(TAG, "Button not handled: %d", btn);
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

    // VFS章节协议初始化 - 移除了位图协议依赖
    // VFS系统会在后台处理章节协议，无需额外初始化
    ESP_LOGI(TAG, "VFS chapter protocol ready");

    // 初始化蓝牙管理器（需要大块连续内存）
    if (!ble_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize BLE manager");
        deinit_x4im_mutex();
        return;
    }

    // BLE初始化成功后，再分配Page buffer（避免堆碎片化）
    // 注意：如果内存碎片导致分配失败，系统会直接从文件读到帧缓冲，但不会中断BLE
    if (!init_page_buffer()) {
        ESP_LOGW(TAG, "Page buffer allocation failed due to fragmentation, will read pages directly to framebuffer");
    }

    // 初始化VFS系统
    vfs_init();
    
    // 打开BLE虚拟文件（使用当前的书籍/章节哈希）
    char vfs_uri[256];
    snprintf(vfs_uri, sizeof(vfs_uri), "ble://weread_novel");
    s_ble_state.vfs_book = vfs_open(vfs_uri);
    
    if (s_ble_state.vfs_book != NULL) {
        // 设置总章节数 (1章节或多章节取决于实际内容)
        vfs_set_total_chapters(s_ble_state.vfs_book, 1);
        vfs_set_prefetch_window(s_ble_state.vfs_book, 5);  // 预加载5页
        ESP_LOGI(TAG, "VFS book opened successfully");
    } else {
        ESP_LOGW(TAG, "Failed to open VFS book");
    }

    // 初始化VFS三页窗口目录并重置状态
    vfs_page_init();
    vfs_prepare_window((int32_t)s_ble_state.current_page);

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
    // 位图协议已废弃，无需清理

    // 关闭VFS虚拟文件
    if (s_ble_state.vfs_book != NULL) {
        vfs_close(s_ble_state.vfs_book);
        s_ble_state.vfs_book = NULL;
        ESP_LOGI(TAG, "VFS book closed");
    }

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

void __attribute__((unused)) ble_reader_screen_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from device");

    if (s_ble_state.device_connected) {
        s_ble_state.device_connected = false;
        s_ble_state.state = BLE_READER_STATE_IDLE;
        ble_manager_disconnect();
    }
}

/**
 * @brief 切换到指定章节（公开API）
 * @param chapter_index 章节索引（0-based）
 * @return true 成功，false 失败
 */
bool __attribute__((unused)) ble_reader_switch_chapter(int chapter_index)
{
    if (s_ble_state.vfs_book == NULL) {
        ESP_LOGE(TAG, "VFS book not initialized");
        return false;
    }
    
    if (vfs_switch_chapter(s_ble_state.vfs_book, chapter_index)) {
        // 重置页面位置到新章节开头
        s_ble_state.current_page = 0;
        s_ble_state.char_position = 0;
        s_ble_state.history_len = 0;
        
        ESP_LOGI(TAG, "Switched to chapter %d", chapter_index);
        update_cached_window(0);
        send_position_snapshot();
        send_page_sync_notification(0);
        
        // 刷新屏幕
        draw_reading_mode_screen(false);
        display_refresh(REFRESH_MODE_FULL);
        return true;
    }
    
    return false;
}

/**
 * @brief 获取当前章节
 * @return 章节索引（0-based），-1表示错误
 */
int __attribute__((unused)) ble_reader_get_current_chapter(void)
{
    if (s_ble_state.vfs_book == NULL) {
        return -1;
    }
    return vfs_get_current_chapter(s_ble_state.vfs_book);
}

/**
 * @brief 获取总章节数
 * @return 章节数，-1表示未知或错误
 */
int __attribute__((unused)) ble_reader_get_total_chapters(void)
{
    if (s_ble_state.vfs_book == NULL) {
        return -1;
    }
    return vfs_get_total_chapters(s_ble_state.vfs_book);
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

// ============================================================================
// 章节浏览实现
// ============================================================================

/**
 * @brief 请求章节列表（发送 LIST_CHAPTERS 命令）
 */
static void request_chapter_list(void) {
    if (!s_ble_state.device_connected) {
        ESP_LOGW(TAG, "Device not connected, cannot request chapter list");
        return;
    }
    
    // 构建 LIST_CHAPTERS 请求
    uint8_t cmd[64];
    int len = 0;
    
    cmd[len++] = 0xA5;  // Magic 0
    cmd[len++] = 0x5A;  // Magic 1
    cmd[len++] = X4IM_CMD_LIST_CHAPTERS;  // 0x99
    
    // 书籍ID（使用当前book_hash的字符串表示）
    char book_id[32];
    snprintf(book_id, sizeof(book_id), "0x%08lX", (unsigned long)s_ble_state.current_book_hash);
    int book_id_len = strlen(book_id);
    cmd[len++] = (uint8_t)book_id_len;
    memcpy(&cmd[len], book_id, book_id_len);
    len += book_id_len;
    
    // 发送请求
    ble_manager_send_notification(cmd, len);
    
    ESP_LOGI(TAG, "📚 Requested chapter list for book: %s", book_id);
}

/**
 * @brief 解析章节信息JSON
 * @param json_data JSON字符串
 * @param json_len JSON长度
 */
static void parse_chapter_info_json(const char *json_data, size_t json_len) {
    // 简单的JSON解析（实际项目中应使用cJSON库）
    // 预期格式: {"book_title":"三体","chapters":[{"index":0,"title":"第一章","page_count":45,"available":true},...]}
    
    // 重置章节列表
    s_ble_state.chapter_list_count = 0;
    memset(s_ble_state.book_title, 0, sizeof(s_ble_state.book_title));
    
    // 提取书名（查找 "book_title":"..."）
    const char *title_start = strstr(json_data, "\"book_title\":\"");
    if (title_start) {
        title_start += 14;  // 跳过 "book_title":"
        const char *title_end = strchr(title_start, '"');
        if (title_end) {
            int title_len = title_end - title_start;
            if (title_len > 0 && title_len < sizeof(s_ble_state.book_title)) {
                memcpy(s_ble_state.book_title, title_start, title_len);
                s_ble_state.book_title[title_len] = '\0';
            }
        }
    }
    
    // 提取章节数组（查找 "chapters":[...]）
    const char *chapters_start = strstr(json_data, "\"chapters\":[");
    if (!chapters_start) {
        ESP_LOGW(TAG, "No chapters array found in JSON");
        return;
    }
    
    chapters_start += 12;  // 跳过 "chapters":[
    const char *ptr = chapters_start;
    
    // 解析每个章节对象
    while (*ptr && s_ble_state.chapter_list_count < 64) {
        // 查找下一个 {
        ptr = strchr(ptr, '{');
        if (!ptr) break;
        ptr++;
        
        // 解析 index
        const char *index_str = strstr(ptr, "\"index\":");
        if (index_str) {
            index_str += 8;
            s_ble_state.chapter_list[s_ble_state.chapter_list_count].index = atoi(index_str);
        }
        
        // 解析 title
        const char *title_str = strstr(ptr, "\"title\":\"");
        if (title_str) {
            title_str += 9;
            const char *title_end = strchr(title_str, '"');
            if (title_end) {
                int len = title_end - title_str;
                if (len > 0 && len < 128) {
                    memcpy(s_ble_state.chapter_list[s_ble_state.chapter_list_count].title, 
                           title_str, len);
                    s_ble_state.chapter_list[s_ble_state.chapter_list_count].title[len] = '\0';
                }
            }
        }
        
        // 解析 page_count
        const char *pages_str = strstr(ptr, "\"page_count\":");
        if (pages_str) {
            pages_str += 13;
            s_ble_state.chapter_list[s_ble_state.chapter_list_count].page_count = atoi(pages_str);
        }
        
        // 解析 available (默认所有章节都可读)
        s_ble_state.chapter_list[s_ble_state.chapter_list_count].available = true;
        
        s_ble_state.chapter_list_count++;
        
        // 查找下一个对象
        ptr = strchr(ptr, '}');
        if (!ptr) break;
        ptr++;
    }
    
    ESP_LOGI(TAG, "📖 Parsed %d chapters from JSON", s_ble_state.chapter_list_count);
    if (s_ble_state.book_title[0]) {
        ESP_LOGI(TAG, "   Book: %s", s_ble_state.book_title);
    }
}

/**
 * @brief 进入章节浏览模式
 */
static void enter_chapter_browser(void) {
    ESP_LOGI(TAG, "Entering chapter browser mode");
    
    s_ble_state.chapter_browser_active = true;
    s_ble_state.chapter_list_selection = vfs_get_current_chapter(s_ble_state.vfs_book);
    
    // 如果还没有章节列表，请求一次
    if (s_ble_state.chapter_list_count == 0) {
        request_chapter_list();
        
        // 显示加载提示
        display_clear(0xFF);
        display_draw_text_font(SCREEN_WIDTH/2 - 100, 60, "正在获取章节列表...", &Font20, 0, 0xFF);
        display_refresh(REFRESH_MODE_FULL);
    } else {
        // 直接显示章节列表
        draw_chapter_browser_screen();
        display_refresh(REFRESH_MODE_FULL);
    }
}

/**
 * @brief 退出章节浏览模式
 */
static void exit_chapter_browser(void) {
    ESP_LOGI(TAG, "Exiting chapter browser mode");
    
    s_ble_state.chapter_browser_active = false;
    
    // 返回阅读界面
    draw_reading_mode_screen(false);
    display_refresh(REFRESH_MODE_FULL);
}

/**
 * @brief 绘制章节浏览界面
 */
static void draw_chapter_browser_screen(void) {
    display_clear(0xFF);
    
    // 标题栏
    char title_buf[160];  // 增加缓冲区大小避免截断
    if (s_ble_state.book_title[0]) {
        snprintf(title_buf, sizeof(title_buf), "%s (共%d章)", 
                 s_ble_state.book_title, s_ble_state.chapter_list_count);
    } else {
        snprintf(title_buf, sizeof(title_buf), "章节选择 (共%d章)", 
                 s_ble_state.chapter_list_count);
    }
    display_draw_text_font(10, 10, title_buf, &Font16, 0, 0xFF);
    
    if (s_ble_state.chapter_list_count == 0) {
        // 无章节数据
        display_draw_text_font(SCREEN_WIDTH/2 - 60, 100, "无可用章节", &Font20, 0, 0xFF);
        display_draw_text_font(SCREEN_WIDTH/2 - 70, 130, "按任意键返回", &Font16, 0, 0xFF);
        return;
    }
    
    // 显示章节列表（最多显示8个）
    const int MAX_VISIBLE = 8;
    const int LINE_HEIGHT = 22;
    const int START_Y = 40;
    
    // 计算显示范围
    int start_idx = s_ble_state.chapter_list_selection;
    if (start_idx > s_ble_state.chapter_list_count - MAX_VISIBLE) {
        start_idx = s_ble_state.chapter_list_count - MAX_VISIBLE;
    }
    if (start_idx < 0) start_idx = 0;
    
    int end_idx = start_idx + MAX_VISIBLE;
    if (end_idx > s_ble_state.chapter_list_count) {
        end_idx = s_ble_state.chapter_list_count;
    }
    
    // 绘制章节列表
    int display_row = 0;
    for (int i = start_idx; i < end_idx; i++) {
        // 只显示可用的章节
        if (!s_ble_state.chapter_list[i].available) {
            continue;
        }
        
        int y = START_Y + display_row * LINE_HEIGHT;
        char line_buf[96];
        
        // 选中标记
        const char *marker = (i == s_ble_state.chapter_list_selection) ? ">" : " ";
        
        // 章节标题（截断长标题）
        snprintf(line_buf, sizeof(line_buf), "%s %d. %.40s", 
                 marker, i + 1, s_ble_state.chapter_list[i].title);
        
        // 使用反色表示选中状态
        if (i == s_ble_state.chapter_list_selection) {
            display_draw_text_font(5, y, line_buf, &Font16, 0xFF, 0);
        } else {
            display_draw_text_font(10, y, line_buf, &Font16, 0, 0xFF);
        }
        
        display_row++;
    }
    
    // 底部提示
    display_draw_text_font(10, SCREEN_HEIGHT - 20, "Vol+/-选择  OK选中  Back返回", &Font12, 0, 0xFF);
}

/**
 * @brief 处理章节浏览模式的按键
 */
static void handle_chapter_browser_button(screen_t *screen, button_t btn) {
    switch (btn) {
        case BTN_VOLUME_UP:  // 上一章
            if (s_ble_state.chapter_list_selection > 0) {
                // 跳过不可用的章节
                do {
                    s_ble_state.chapter_list_selection--;
                } while (s_ble_state.chapter_list_selection >= 0 && 
                         !s_ble_state.chapter_list[s_ble_state.chapter_list_selection].available);
                
                if (s_ble_state.chapter_list_selection < 0) {
                    s_ble_state.chapter_list_selection = 0;
                }
                
                draw_chapter_browser_screen();
                display_refresh(REFRESH_MODE_PARTIAL);
            }
            break;
            
        case BTN_VOLUME_DOWN:  // 下一章
            if (s_ble_state.chapter_list_selection < s_ble_state.chapter_list_count - 1) {
                // 跳过不可用的章节
                do {
                    s_ble_state.chapter_list_selection++;
                } while (s_ble_state.chapter_list_selection < s_ble_state.chapter_list_count && 
                         !s_ble_state.chapter_list[s_ble_state.chapter_list_selection].available);
                
                if (s_ble_state.chapter_list_selection >= s_ble_state.chapter_list_count) {
                    s_ble_state.chapter_list_selection = s_ble_state.chapter_list_count - 1;
                }
                
                draw_chapter_browser_screen();
                display_refresh(REFRESH_MODE_PARTIAL);
            }
            break;
            
        case BTN_CONFIRM:  // 确认选择
        case BTN_RIGHT:
            if (s_ble_state.chapter_list_count > 0 && 
                s_ble_state.chapter_list[s_ble_state.chapter_list_selection].available) {
                select_and_load_chapter(s_ble_state.chapter_list_selection);
            }
            break;
            
        case BTN_LEFT:  // 返回
        case BTN_BACK:
            exit_chapter_browser();
            break;
            
        default:
            break;
    }
}

/**
 * @brief 选择并加载指定章节
 */
static void select_and_load_chapter(int chapter_index) {
    ESP_LOGI(TAG, "📖 User selected chapter %d: %s", 
             chapter_index + 1, 
             s_ble_state.chapter_list[chapter_index].title);
    
    // 发送 SELECT_CHAPTER 命令到 Client
    uint8_t cmd[4] = {
        0xA5, 0x5A,                    // Magic
        X4IM_CMD_SELECT_CHAPTER,       // 0x9B
        (uint8_t)chapter_index
    };
    ble_manager_send_notification(cmd, sizeof(cmd));
    
    // 切换到该章节
    if (ble_reader_switch_chapter(chapter_index)) {
        // 退出章节浏览模式，进入阅读模式
        s_ble_state.chapter_browser_active = false;
        
        // 显示加载提示
        display_clear(0xFF);
        display_draw_text_font(SCREEN_WIDTH/2 - 80, 100, "正在加载章节...", &Font20, 0, 0xFF);
        display_refresh(REFRESH_MODE_FULL);
        
        // 等待一会让 Client 准备数据
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // 显示章节内容
        draw_reading_mode_screen(false);
        display_refresh(REFRESH_MODE_FULL);
    } else {
        ESP_LOGE(TAG, "Failed to switch to chapter %d", chapter_index);
        
        // 显示错误提示
        display_clear(0xFF);
        display_draw_text_font(SCREEN_WIDTH/2 - 70, 100, "章节加载失败", &Font20, 0, 0xFF);
        display_draw_text_font(SCREEN_WIDTH/2 - 70, 130, "按任意键返回", &Font16, 0, 0xFF);
        display_refresh(REFRESH_MODE_FULL);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 返回章节列表
        draw_chapter_browser_screen();
        display_refresh(REFRESH_MODE_FULL);
    }
}

