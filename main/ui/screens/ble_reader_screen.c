/**
 * @file ble_reader_screen.c
 * @brief 蓝牙读书屏幕实现 - 分页位图显示，支持滑动窗口缓存
 */

#include "ble_reader_screen.h"
#include "ble_manager.h"
#include "ble_book_protocol.h"
#include "ble_cache_manager.h"
#include "display_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

static const char *TAG = "BLE_READER";

// ========== X4IM v2 命令定义 ==========
#define X4IM_CMD_SET_MODE   0x8C    // 设置工作模式
#define X4IM_CMD_GET_MODE   0x8D    // 查询当前模式
#define X4IM_CMD_FILE_NOTIFY 0x8B   // 文件通知
#define X4IM_CMD_LIST_FILES 0x8E    // 列表 SD 卡文件
#define X4IM_CMD_DELETE_FILE 0x8F   // 删除 SD 卡文件
#define X4IM_CMD_RENAME_FILE 0x90   // 重命名 SD 卡文件
#define X4IM_CMD_FORMAT_SD  0x91    // 格式化 SD 卡

// 蓝牙读书屏幕实例（导出以供屏幕管理器注册）
screen_t g_ble_reader_screen = {0};

// 蓝牙读书屏幕状态
typedef struct {
    ble_reader_state_t state;           // 当前状态
    ble_work_mode_t work_mode;          // 工作模式（阅读/传输）
    uint16_t current_book_id;           // 当前书籍ID
    uint16_t current_page;              // 当前显示的页码
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
    
    // ========== 传输模式状态 ==========
    uint32_t transfer_bytes_received;   // 已接收字节数
    uint32_t transfer_bytes_total;      // 总字节数
    char transfer_filename[64];         // 当前传输的文件名
    uint16_t transfer_file_count;       // 已传输文件数
} ble_reader_state_internal_t;

static ble_reader_state_internal_t s_ble_state = {
    .state = BLE_READER_STATE_IDLE,
    .work_mode = BLE_MODE_READING,      // 默认阅读模式
    .current_book_id = 0,
    .current_page = 0,
    .total_pages = 0,
    .device_connected = false,
    .page_loaded = false,
    .preload_requested = false,
    .preload_start_page = 0,
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
static uint16_t s_buffered_book_id = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_show(screen_t *screen);
static void on_hide(screen_t *screen);
static void on_draw(screen_t *screen);
static void on_event(screen_t *screen, button_t btn, button_event_t event);

// 蓝牙回调函数
static void ble_connect_callback(bool connected);
static void ble_data_received_callback(const uint8_t *data, uint16_t length);

// 协议回调
static bool on_page_ready(uint16_t book_id, uint16_t page_num);
static void on_preload_needed(uint16_t book_id, uint16_t start_page, uint8_t page_count);

// 双模式界面绘制
static void draw_transfer_mode_screen(void);
static void draw_reading_mode_screen(void);

// 双模式按键处理
static void handle_transfer_mode_button(screen_t *screen, button_t btn);
static void handle_reading_mode_button(screen_t *screen, button_t btn);

// 构造 /sdcard/books/ 路径，确保字符串不会截断溢出
static void build_books_path(char *out, size_t out_size, const char *name)
{
    const char *base = "/sdcard/books/";
    size_t base_len = strlen(base);
    size_t max_name_len = (out_size > base_len + 1) ? out_size - base_len - 1 : 0;
    size_t name_len = strnlen(name, max_name_len);
    snprintf(out, out_size, "%s%.*s", base, (int)name_len, name);
}

// 翻页防抖和同步
static void send_page_sync_notification(uint16_t page_num);
static void update_cached_window(uint16_t current_page);
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
        s_page_buffer = (uint8_t *)heap_caps_malloc(PAGE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (s_page_buffer == NULL) {
            // 尝试从内部RAM分配
            s_page_buffer = (uint8_t *)malloc(PAGE_BUFFER_SIZE);
        }
        if (s_page_buffer != NULL) {
            ESP_LOGI(TAG, "Page buffer allocated at %p (%zu bytes)",
                     s_page_buffer, PAGE_BUFFER_SIZE);
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

/**
 * @brief 标记页面已加载（实际数据在littlefs中，不预加载）
 */
static bool load_current_page(void)
{
    if (s_ble_state.current_book_id == 0) {
        s_ble_state.page_loaded = false;
        return false;
    }

    // 检查页面缓存是否存在（仅检查，不加载到内存）
    // 页面数据存储在littlefs中，on_draw时按需读取
    char filename[64];
    snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
             s_ble_state.current_book_id, s_ble_state.current_page);
    
    FILE *f = fopen(filename, "rb");
    if (f != NULL) {
        fclose(f);
        s_ble_state.page_loaded = true;
        
        // 更新阅读位置，触发预加载检查
        ble_cache_update_read_position(s_ble_state.current_book_id, 
                                       s_ble_state.current_page);
        return true;
    }

    ESP_LOGW(TAG, "Page file not found: book=%04x, page=%u",
             s_ble_state.current_book_id, s_ble_state.current_page);
    s_ble_state.page_loaded = false;
    return false;
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
    bool use_sd_card;           // 是否写入SD卡（传输模式）
    uint16_t flags;             // X4IM v2 flags
} x4im_rx_state = {0};

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
    
    // 删除互斥锁
    if (x4im_rx_mutex != NULL) {
        vSemaphoreDelete(x4im_rx_mutex);
        x4im_rx_mutex = NULL;
    }
}

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

    // ========== 处理模式控制命令 ==========
    // SET_MODE 命令：2字节 [0x8C, mode]
    if (length == 2 && data[0] == X4IM_CMD_SET_MODE) {
        uint8_t new_mode = data[1];
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
    
    // GET_MODE 命令：1字节 [0x8D]
    if (length == 1 && data[0] == X4IM_CMD_GET_MODE) {
        ESP_LOGI(TAG, "Received GET_MODE query");
        
        // 发送当前模式 [0x8D, mode]
        uint8_t response[2] = {X4IM_CMD_GET_MODE, s_ble_state.work_mode};
        ble_manager_send_data(response, 2);
        ESP_LOGI(TAG, "Sent current mode: %s",
                 s_ble_state.work_mode == BLE_MODE_READING ? "READING" : "TRANSFER");
        
        free((void *)data);
        return;
    }

    // ========== SD 卡文件管理命令 ==========
    // LIST_FILES 命令：1字节 [0x8E]
    if (length == 1 && data[0] == X4IM_CMD_LIST_FILES) {
        ESP_LOGI(TAG, "Received LIST_FILES command");
        
        DIR *dir = opendir("/sdcard/books");
        if (dir == NULL) {
            ESP_LOGW(TAG, "Failed to open /sdcard/books directory");
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x00};  // 0=empty
            ble_manager_send_data(response, 2);
            free((void *)data);
            return;
        }
        
        // 发送文件列表：每个文件一条消息 [0x8B, 文件大小(4字节), 文件名...]
        struct dirent *entry;
        int file_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {  // 普通文件
                char filepath[256];
                build_books_path(filepath, sizeof(filepath), entry->d_name);
                
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    uint8_t response[300];
                    response[0] = X4IM_CMD_FILE_NOTIFY;
                    
                    // 编码文件大小（4字节，小端）
                    uint32_t size = st.st_size;
                    response[1] = (size >> 0) & 0xFF;
                    response[2] = (size >> 8) & 0xFF;
                    response[3] = (size >> 16) & 0xFF;
                    response[4] = (size >> 24) & 0xFF;
                    
                    // 文件名
                    int name_len = strlen(entry->d_name);
                    if (name_len > 255 - 5) name_len = 255 - 5;
                    memcpy(&response[5], entry->d_name, name_len);
                    
                    ble_manager_send_data(response, 5 + name_len);
                    file_count++;
                    ESP_LOGI(TAG, "  File: %s (%u bytes)", entry->d_name, (unsigned)size);
                }
            }
        }
        closedir(dir);
        
        // 发送文件列表结束标记
        uint8_t end_marker[2] = {X4IM_CMD_FILE_NOTIFY, 0xFF};
        ble_manager_send_data(end_marker, 2);
        ESP_LOGI(TAG, "Sent %d files", file_count);
        
        free((void *)data);
        return;
    }

    // DELETE_FILE 命令：[0x8F, 文件名...]
    if (length > 1 && data[0] == X4IM_CMD_DELETE_FILE) {
        char filename[256];
        int name_len = length - 1;
        if (name_len > 255) name_len = 255;
        memcpy(filename, &data[1], name_len);
        filename[name_len] = '\0';
        
        char filepath[256];
        build_books_path(filepath, sizeof(filepath), filename);
        
        if (unlink(filepath) == 0) {
            ESP_LOGI(TAG, "Deleted file: %s", filepath);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
            ble_manager_send_data(response, 2);
        } else {
            ESP_LOGW(TAG, "Failed to delete file: %s (errno=%d)", filepath, errno);
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
        }
        
        free((void *)data);
        return;
    }

    // FORMAT_SD 命令：1字节 [0x91]
    if (length == 1 && data[0] == X4IM_CMD_FORMAT_SD) {
        ESP_LOGW(TAG, "Received FORMAT_SD command - deleting all files in /sdcard/books");
        
        DIR *dir = opendir("/sdcard/books");
        if (dir != NULL) {
            struct dirent *entry;
            int deleted = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {
                    char filepath[256];
                    build_books_path(filepath, sizeof(filepath), entry->d_name);
                    if (unlink(filepath) == 0) {
                        deleted++;
                    }
                }
            }
            closedir(dir);
            ESP_LOGI(TAG, "Deleted %d files from /sdcard/books", deleted);
            
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x01};  // 1=success
            ble_manager_send_data(response, 2);
        } else {
            uint8_t response[2] = {X4IM_CMD_FILE_NOTIFY, 0x02};  // 2=failed
            ble_manager_send_data(response, 2);
        }
        
        free((void *)data);
        return;
    }

    // 检查是否是 X4IM 帧头 (v2: 32字节, v1: 12字节 兼容)  
    // "X4IM" + version(2) + flags(2) + payload_size(4) + sequence(2) + reserved(2) + filename(16)
    // X4IM协议处理
    if (length >= X4IM_HEADER_SIZE_V1 && data[0] == 'X' && data[1] == '4' &&
        data[2] == 'I' && data[3] == 'M') {

        // 解析帧头
        uint16_t flags = data[6] | (data[7] << 8);
        uint32_t payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
        
        // 判断是否是 v2 协议（32 字节头）
        bool is_v2 = (length >= X4IM_HEADER_SIZE);
        int header_size = is_v2 ? X4IM_HEADER_SIZE : X4IM_HEADER_SIZE_V1;
        
        // 提取文件名（v2 协议，偏移 16，最多 15 字符）
        char recv_filename[16] = {0};
        if (is_v2 && length >= X4IM_HEADER_SIZE) {
            memcpy(recv_filename, &data[16], 15);
            recv_filename[15] = '\0';
        }
        
        // 检查是否需要存储到 SD 卡
        bool use_sd = (flags & X4IM_FLAGS_STORAGE_SD) != 0;
        
        ESP_LOGI(TAG, "X4IM frame: v%d, flags=0x%04X, payload=%" PRIu32 ", sd=%d, name='%s'",
             is_v2 ? 2 : 1, flags, payload_size, use_sd, recv_filename);

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
            
            if (use_sd) {
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
                // LittleFS 路径：阅读模式缓存
                snprintf(x4im_rx_state.filename, sizeof(x4im_rx_state.filename), 
                         "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                         s_ble_state.current_book_id ? s_ble_state.current_book_id : 1,
                         s_ble_state.current_page);
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
            } else {
                xSemaphoreGive(x4im_rx_mutex);
            }
        }
        
        // 处理完毕，释放内存
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
                free((void *)data);  // 释放内存
                x4im_rx_state.file_handle = NULL;
                x4im_rx_state.receiving = false;
                xSemaphoreGive(x4im_rx_mutex);
                return;
            }

            x4im_rx_state.received_size += copy_len;
            
            // 传输模式：更新进度
            if (s_ble_state.work_mode == BLE_MODE_TRANSFER) {
                s_ble_state.transfer_bytes_received = x4im_rx_state.received_size;
            }

            ESP_LOGI(TAG, "Streaming to file: %" PRIu32 "/%" PRIu32 " bytes (%.1f%%)",
                     x4im_rx_state.received_size, x4im_rx_state.expected_size,
                     (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size);

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

                    // 标记页面已加载
                    s_ble_state.page_loaded = true;
                }

                // 触发重绘
                screen_t *screen = screen_manager_get_current();
                if (screen != NULL && screen == &g_ble_reader_screen) {
                    screen->needs_redraw = true;
                    screen_manager_draw();
                }
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
            }
        } else {
            xSemaphoreGive(x4im_rx_mutex);
        }
    }
    
    // 所有路径最后都要释放内存
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
 * @brief 更新三页缓存窗口 (prev, current, next)
 * 并检查缓存中是否存在，不存在则向手机请求
 */
static void update_cached_window(uint16_t current_page)
{
    // 更新缓存页码记录
    s_ble_state.cached_pages[0] = (current_page > 0) ? current_page - 1 : 0;
    s_ble_state.cached_pages[1] = current_page;
    s_ble_state.cached_pages[2] = current_page + 1;

    ESP_LOGI(TAG, "Updated cache window: prev=%u, current=%u, next=%u",
             s_ble_state.cached_pages[0],
             s_ble_state.cached_pages[1],
             s_ble_state.cached_pages[2]);
    
    // 检查三页缓存是否存在，不存在则请求手机发送
    for (int i = 0; i < 3; i++) {
        uint16_t page = s_ble_state.cached_pages[i];
        
        // 检查该页是否已缓存
        char filename[64];
        snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                 s_ble_state.current_book_id, page);
        
        FILE *f = fopen(filename, "rb");
        if (f == NULL) {
            // 缓存不存在，请求手机发送该页
            ESP_LOGI(TAG, "Cache miss for page %u, already sent PAGE:%u notification", page, current_page);
            // 注意：send_page_sync_notification(current_page) 已在 on_event() 中调用
            // 手机会根据 current_page 自动发送 [page-1, page, page+1] 三页
        } else {
            fclose(f);
            ESP_LOGI(TAG, "Cache hit for page %u", page);
        }
    }
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
        draw_reading_mode_screen();
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
 * @brief 绘制阅读模式界面
 */
static void draw_reading_mode_screen(void)
{
    // 绘制标题栏
    int title_y = 20;
    display_draw_text_menu(20, title_y, "蓝牙读书", COLOR_BLACK, COLOR_WHITE);

    // 绘制连接状态
    int status_y = 60;
    const char *status_str = NULL;
    switch (s_ble_state.state) {
        case BLE_READER_STATE_IDLE:
            status_str = "状态: 空闲";
            break;
        case BLE_READER_STATE_WAITING:
            status_str = "状态: 等待连接...";
            break;
        case BLE_READER_STATE_CONNECTING:
            status_str = "状态: 连接中...";
            break;
        case BLE_READER_STATE_CONNECTED:
            status_str = "状态: 已连接";
            break;
        case BLE_READER_STATE_RECEIVING:
            status_str = "状态: 接收中...";
            break;
        case BLE_READER_STATE_READING:
            status_str = "状态: 阅读中";
            break;
        default:
            status_str = "状态: 未知";
            break;
    }
    display_draw_text_menu(20, status_y, status_str, COLOR_BLACK, COLOR_WHITE);

    // 绘制页面内容（直接显示缓存，不等待 page_loaded）
    if (s_ble_state.current_book_id != 0) {
        // 检查内存缓冲中是否已经是当前页
        bool buffer_valid = (s_page_buffer != NULL) &&
                           (s_buffered_book_id == s_ble_state.current_book_id) &&
                           (s_buffered_page_id == s_ble_state.current_page);
        
        if (buffer_valid) {
            // 缓存命中，直接绘制
            uint8_t *framebuffer = display_get_framebuffer();
            if (framebuffer != NULL) {
                memcpy(framebuffer, s_page_buffer, PAGE_BUFFER_SIZE);
            }
        } else {
            // 缓存未命中，需要从文件读取
            char filename[64];
            snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                     s_ble_state.current_book_id, s_ble_state.current_page);
    
            FILE *f = fopen(filename, "rb");
            if (f != NULL) {
                // 使用静态分配的页面缓冲区
                if (s_page_buffer != NULL) {
                    size_t read = fread(s_page_buffer, 1, PAGE_BUFFER_SIZE, f);
                    if (read > 0) {
                        // 读取成功，更新缓冲区状态
                        s_buffered_book_id = s_ble_state.current_book_id;
                        s_buffered_page_id = s_ble_state.current_page;
                        
                        // 复制到帧缓冲
                        uint8_t *framebuffer = display_get_framebuffer();
                        if (framebuffer != NULL) {
                            memcpy(framebuffer, s_page_buffer, read);
                            ESP_LOGI(TAG, "Bitmap loaded from file and displayed");
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Page buffer not allocated");
                }
                fclose(f);
            } else {
                // 页面缺失，显示提示
                // 重置缓冲区状态，因为它不匹配当前页
                s_buffered_page_id = 0xFFFF;
                
                display_draw_text_menu(20, 100, "缓存缺失", COLOR_BLACK, COLOR_WHITE);
                display_draw_text_menu(20, 140, "正在从手机请求...", COLOR_BLACK, COLOR_WHITE);
                
                // 如果正在接收数据，显示进度
                if (x4im_rx_state.receiving && x4im_rx_state.expected_size > 0) {
                    char progress[64];
                    float percent = (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size;
                    snprintf(progress, sizeof(progress), "接收中: %.0f%% (%lu/%lu 字节)",
                             percent, (unsigned long)x4im_rx_state.received_size, (unsigned long)x4im_rx_state.expected_size);
                    display_draw_text_menu(20, 180, progress, COLOR_BLACK, COLOR_WHITE);
                }
            }
        }
    } else {
        display_draw_text_menu(20, 100, "未选择书籍", COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, 140, "等待手机发送内容...", COLOR_BLACK, COLOR_WHITE);
    }

    // 绘制页码信息
    if (s_ble_state.current_book_id != 0) {
        char page_info[64];
        if (s_ble_state.total_pages > 0) {
            snprintf(page_info, sizeof(page_info), "第 %u / %u 页",
                     s_ble_state.current_page + 1, s_ble_state.total_pages);
        } else {
            snprintf(page_info, sizeof(page_info), "第 %u 页",
                     s_ble_state.current_page + 1);
        }
        display_draw_text_menu(20, SCREEN_HEIGHT - 60, page_info, COLOR_BLACK, COLOR_WHITE);
    }

    // 显示初始化确认提示
    if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
        display_draw_text_menu(20, SCREEN_HEIGHT / 2 - 40,
                               "点击确认开始阅读",
                               COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, SCREEN_HEIGHT / 2,
                               "按 确认 键",
                               COLOR_BLACK, COLOR_WHITE);
        s_ble_state.showing_confirm_prompt = true;
    } else {
        s_ble_state.showing_confirm_prompt = false;
    }

    // 绘制底部提示
    if (!s_ble_state.initialization_complete && s_ble_state.current_book_id != 0) {
        display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                               "确认: 开始",
                               COLOR_BLACK, COLOR_WHITE);
    } else {
        display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                               "上: 上页  下: 下页  返回: 退出",
                               COLOR_BLACK, COLOR_WHITE);
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
    } else {
        // ==================== 阅读模式按键处理 ====================
        handle_reading_mode_button(screen, btn);
    }

    screen->needs_redraw = true;
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

/**
 * @brief 阅读模式按键处理
 */
static void handle_reading_mode_button(screen_t *screen, button_t btn)
{
    switch (btn) {
        case BTN_LEFT:
        case BTN_VOLUME_UP:
            // 上一页 - 立即翻页（有缓存就显示）
            if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
                break; // 在确认前不响应翻页
            }
            if (s_ble_state.current_page > 0) {
                // 立即翻到上一页（无防抖，有缓存直接显示）
                s_ble_state.current_page--;
                
                ESP_LOGI(TAG, "Page turned to: %u (UP)", s_ble_state.current_page);
                
                // 更新缓存窗口（预加载前后页）
                update_cached_window(s_ble_state.current_page);
                
                // 请求手机发送缓存窗口内的页面（前、当前、后）
                send_page_sync_notification(s_ble_state.current_page);
            }
            break;

        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            // 下一页 - 立即翻页（有缓存就显示）
            if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
                break; // 在确认前不响应翻页
            }
            if (s_ble_state.total_pages == 0 || 
                s_ble_state.current_page < s_ble_state.total_pages - 1) {
                // 立即翻到下一页（无防抖，有缓存直接显示）
                s_ble_state.current_page++;
                
                ESP_LOGI(TAG, "Page turned to: %u (DOWN)", s_ble_state.current_page);
                
                // 更新缓存窗口（预加载前后页）
                update_cached_window(s_ble_state.current_page);
                
                // 请求手机发送缓存窗口内的页面（前、当前、后）
                send_page_sync_notification(s_ble_state.current_page);
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
    if (!init_page_buffer()) {
        ESP_LOGE(TAG, "Failed to initialize page buffer");
        ble_manager_deinit();
        ble_book_protocol_deinit();
        deinit_x4im_mutex();
        return;
    }

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

screen_t* ble_reader_screen_get_instance(void)
{
    if (g_ble_reader_screen.name == NULL) {
        ble_reader_screen_init();
    }
    return &g_ble_reader_screen;
}

ble_reader_state_t ble_reader_screen_get_state(void)
{
    return s_ble_state.state;
}

bool ble_reader_screen_connect_device(const uint8_t *addr)
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

void ble_reader_screen_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from device");

    if (s_ble_state.device_connected) {
        s_ble_state.device_connected = false;
        s_ble_state.state = BLE_READER_STATE_IDLE;
        ble_manager_disconnect();
    }
}

void ble_reader_screen_set_current_book(uint16_t book_id)
{
    s_ble_state.current_book_id = book_id;
    s_ble_state.current_page = 0;
    s_ble_state.state = BLE_READER_STATE_READING;

    // 尝试加载第一页
    load_current_page();
}

void ble_reader_screen_goto_page(uint16_t page_num)
{
    if (s_ble_state.current_book_id == 0) {
        ESP_LOGW(TAG, "No book selected");
        return;
    }

    s_ble_state.current_page = page_num;
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

void ble_reader_screen_next_page(void)
{
    if (s_ble_state.current_book_id == 0) {
        return;
    }

    if (s_ble_state.total_pages > 0 && 
        s_ble_state.current_page >= s_ble_state.total_pages - 1) {
        return;  // 已经在最后一页
    }

    s_ble_state.current_page++;
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

void ble_reader_screen_prev_page(void)
{
    if (s_ble_state.current_book_id == 0 || s_ble_state.current_page == 0) {
        return;
    }

    s_ble_state.current_page--;
    load_current_page();

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
    }
}

/**
 * @brief 设置 BLE 工作模式
 */
void ble_reader_set_mode(ble_work_mode_t mode)
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
ble_work_mode_t ble_reader_get_mode(void)
{
    return s_ble_state.work_mode;
}
