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
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "BLE_READER";

// 蓝牙读书屏幕实例（导出以供屏幕管理器注册）
screen_t g_ble_reader_screen = {0};

// 蓝牙读书屏幕状态
typedef struct {
    ble_reader_state_t state;           // 当前状态
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
} ble_reader_state_internal_t;

static ble_reader_state_internal_t s_ble_state = {
    .state = BLE_READER_STATE_IDLE,
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
static void ble_device_found_callback(const ble_device_info_t *device);
static void ble_connect_callback(bool connected);
static void ble_data_received_callback(const uint8_t *data, uint16_t length);

// 协议回调
static bool on_page_ready(uint16_t book_id, uint16_t page_num);
static void on_preload_needed(uint16_t book_id, uint16_t start_page, uint8_t page_count);

// 翻页防抖和同步
static void send_page_sync_notification(uint16_t page_num);
static void update_cached_window(uint16_t current_page);
static void cleanup_old_pages(uint16_t current_page);

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
 * @brief 蓝牙设备发现回调
 */
static void ble_device_found_callback(const ble_device_info_t *device)
{
    if (device == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Device found: %s [%02x:%02x:%02x:%02x:%02x:%02x] RSSI=%d",
             device->name[0] != '\0' ? device->name : "Unknown",
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5],
             device->rssi);
    // 服务器模式：EPD 设备不扫描，等待手机连接
    // 此回调不应触发
    ESP_LOGW(TAG, "Unexpected device found callback in server mode");
    (void)device;
}

/**
 * @brief 蓝牙连接状态回调
 */
static void ble_connect_callback(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "BLE device connected!");
        s_ble_state.state = BLE_READER_STATE_CONNECTED;
        s_ble_state.device_connected = true;
    } else {
        ESP_LOGI(TAG, "BLE device disconnected");
        s_ble_state.state = BLE_READER_STATE_IDLE;
        s_ble_state.device_connected = false;
    }

    screen_t *screen = screen_manager_get_current();
    if (screen != NULL && screen == &g_ble_reader_screen) {
        screen->needs_redraw = true;
        // 立即触发屏幕刷新，显示连接状态变化
        screen_manager_draw();
    }
}

// X4IM 协议接收状态
static struct {
    bool receiving;
    uint32_t expected_size;
    uint32_t received_size;
    uint8_t *buffer;
    uint16_t current_page;
} x4im_rx_state = {0};

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
 * @brief 释放 X4IM 接收互斥锁
 */
static void deinit_x4im_mutex(void)
{
    if (x4im_rx_mutex != NULL) {
        vSemaphoreDelete(x4im_rx_mutex);
        x4im_rx_mutex = NULL;
    }
}

/**
 * @brief 蓝牙数据接收回调 - 支持 X4IM 位图协议
 */
static void ble_data_received_callback(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        ESP_LOGW(TAG, "Received NULL or empty data");
        return;
    }

    ESP_LOGI(TAG, "===== BLE DATA RECEIVED: %u bytes =====", length);
    ESP_LOGI(TAG, "First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X",
             data[0], data[1], data[2], data[3]);

    // 检查是否是 X4IM 帧头 (12字节: "X4IM" + version + flags + payload_size)
    if (length >= 12 && data[0] == 'X' && data[1] == '4' &&
        data[2] == 'I' && data[3] == 'M') {

        // 解析帧头
        uint32_t payload_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

        ESP_LOGI(TAG, "X4IM frame header: payload_size=%u", payload_size);

        // 获取互斥锁，初始化接收状态
        if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 释放旧缓冲区
            if (x4im_rx_state.buffer != NULL) {
                free(x4im_rx_state.buffer);
            }

            x4im_rx_state.buffer = (uint8_t *)malloc(payload_size);
            if (x4im_rx_state.buffer == NULL) {
                xSemaphoreGive(x4im_rx_mutex);
                ESP_LOGE(TAG, "Failed to allocate %u bytes for bitmap", payload_size);
                return;
            }

            x4im_rx_state.expected_size = payload_size;
            x4im_rx_state.received_size = 0;
            x4im_rx_state.receiving = true;

            // 如果帧头后还有数据，复制到缓冲区
            if (length > 12) {
                uint32_t copy_len = length - 12;
                if (copy_len > payload_size) {
                    copy_len = payload_size;
                }
                memcpy(x4im_rx_state.buffer, data + 12, copy_len);
                x4im_rx_state.received_size = copy_len;
                ESP_LOGI(TAG, "Copied %u bytes from header packet (%u/%u)",
                         copy_len, x4im_rx_state.received_size, x4im_rx_state.expected_size);
            }

            // 检查是否已完成
            bool complete = (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            xSemaphoreGive(x4im_rx_mutex);

            if (complete) {
                ESP_LOGI(TAG, "Bitmap received in single packet!");

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

                // 保存位图到 LittleFS（复制缓冲区内容后释放锁）
                char filename[64];
                snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                         s_ble_state.current_book_id ? s_ble_state.current_book_id : 1,
                         s_ble_state.current_page);

                // 获取缓冲区副本
                uint8_t *buffer_copy = NULL;
                uint32_t recv_size = 0;
                if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (x4im_rx_state.buffer != NULL && x4im_rx_state.received_size > 0) {
                        buffer_copy = (uint8_t *)malloc(x4im_rx_state.received_size);
                        if (buffer_copy != NULL) {
                            memcpy(buffer_copy, x4im_rx_state.buffer, x4im_rx_state.received_size);
                            recv_size = x4im_rx_state.received_size;
                        }
                    }
                    xSemaphoreGive(x4im_rx_mutex);
                }

                if (buffer_copy != NULL) {
                    FILE *f = fopen(filename, "wb");
                    if (f != NULL) {
                        size_t written = fwrite(buffer_copy, 1, recv_size, f);
                        fclose(f);
                        ESP_LOGI(TAG, "======== BITMAP SAVED (Single Packet) ========");
                        ESP_LOGI(TAG, "File: %s", filename);
                        ESP_LOGI(TAG, "Size: %u bytes", written);
                        ESP_LOGI(TAG, "=============================================");

                        // 标记页面已加载
                        s_ble_state.page_loaded = true;

                        // 触发重绘
                        screen_t *screen = screen_manager_get_current();
                        if (screen != NULL && screen == &g_ble_reader_screen) {
                            screen->needs_redraw = true;
                            screen_manager_draw();
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to open %s for writing", filename);
                    }
                    free(buffer_copy);
                }

                // 清理接收状态
                if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free(x4im_rx_state.buffer);
                    x4im_rx_state.buffer = NULL;
                    x4im_rx_state.receiving = false;
                    xSemaphoreGive(x4im_rx_mutex);
                }
            }
        }

        return;
    }

    // 继续接收位图数据
    if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (x4im_rx_state.receiving && x4im_rx_state.buffer != NULL) {
            uint32_t remaining = x4im_rx_state.expected_size - x4im_rx_state.received_size;
            uint32_t copy_len = (length > remaining) ? remaining : length;

            memcpy(x4im_rx_state.buffer + x4im_rx_state.received_size, data, copy_len);
            x4im_rx_state.received_size += copy_len;

            ESP_LOGI(TAG, "Receiving bitmap: %u/%u bytes (%.1f%%)",
                     x4im_rx_state.received_size, x4im_rx_state.expected_size,
                     (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size);

            // 检查是否完成
            bool complete = (x4im_rx_state.received_size >= x4im_rx_state.expected_size);
            xSemaphoreGive(x4im_rx_mutex);

            if (complete) {
                ESP_LOGI(TAG, "Bitmap reception complete!");

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

                // Save bitmap to LittleFS
                char filename[64];
                snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                         s_ble_state.current_book_id ? s_ble_state.current_book_id : 1,
                         s_ble_state.current_page);

                // Optimization: Write directly from receive buffer
                // Risk: File write holding mutex blocks BLE task briefly

                bool written_success = false;
                size_t written_size = 0;
                
                if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    if (x4im_rx_state.buffer != NULL && x4im_rx_state.received_size > 0) {
                        FILE *f = fopen(filename, "wb");
                        if (f != NULL) {
                            written_size = fwrite(x4im_rx_state.buffer, 1, x4im_rx_state.received_size, f);
                            fclose(f);
                            written_success = true;
                        } else {
                            ESP_LOGE(TAG, "Failed to open %s for writing", filename);
                        }
                    }
                    xSemaphoreGive(x4im_rx_mutex);
                }

                if (written_success) {
                    ESP_LOGI(TAG, "Bitmap saved: %s, %u bytes", filename, written_size);

                    // 标记页面已加载
                    s_ble_state.page_loaded = true;

                    // 如果还没初始化，增加页码计数
                    if (!s_ble_state.initialization_complete) {
                        s_ble_state.current_page++;
                        if (s_ble_state.current_page >= 3) {
                            ESP_LOGI(TAG, "Initial 3 pages received!");
                        }
                    }
                    
                    // 清理旧缓存页面
                    cleanup_old_pages(s_ble_state.current_page);

                    // 触发重绘
                    screen_t *screen = screen_manager_get_current();
                    if (screen != NULL && screen == &g_ble_reader_screen) {
                        screen->needs_redraw = true;
                        screen_manager_draw();
                    }
                }

                // Clean up receive state
                if (xSemaphoreTake(x4im_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free(x4im_rx_state.buffer);
                    x4im_rx_state.buffer = NULL;
                    x4im_rx_state.receiving = false;
                    xSemaphoreGive(x4im_rx_mutex);
                }
            }
        }
    }
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
static void cleanup_old_pages(uint16_t current_page)
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
    ESP_LOGI(TAG, "on_draw START");

    if (s_context == NULL) {
        ESP_LOGW(TAG, "s_context is NULL!");
        return;
    }

    // 清屏
    display_clear(COLOR_WHITE);

    // 绘制标题栏
    int title_y = 20;
    display_draw_text_menu(20, title_y, "BLE Book Reader", COLOR_BLACK, COLOR_WHITE);

    // 绘制连接状态
    int status_y = 60;
    const char *status_str = NULL;
    switch (s_ble_state.state) {
        case BLE_READER_STATE_IDLE:
            status_str = "Status: Idle";
            break;
        case BLE_READER_STATE_SCANNING:
            status_str = "Status: Scanning...";
            break;
        case BLE_READER_STATE_CONNECTING:
            status_str = "Status: Connecting...";
            break;
        case BLE_READER_STATE_CONNECTED:
            status_str = "Status: Connected";
            break;
        case BLE_READER_STATE_READING:
            status_str = "Status: Reading";
            break;
        default:
            status_str = "Status: Unknown";
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
                
                display_draw_text_menu(20, 100, "Page cache missed", COLOR_BLACK, COLOR_WHITE);
                display_draw_text_menu(20, 140, "Requesting from phone...", COLOR_BLACK, COLOR_WHITE);
                
                // 如果正在接收数据，显示进度
                if (x4im_rx_state.receiving && x4im_rx_state.expected_size > 0) {
                    char progress[64];
                    float percent = (float)x4im_rx_state.received_size * 100.0f / x4im_rx_state.expected_size;
                    snprintf(progress, sizeof(progress), "Receiving: %.0f%% (%lu/%lu bytes)",
                             percent, (unsigned long)x4im_rx_state.received_size, (unsigned long)x4im_rx_state.expected_size);
                    display_draw_text_menu(20, 180, progress, COLOR_BLACK, COLOR_WHITE);
                }
            }
        }
    } else {
        display_draw_text_menu(20, 100, "No book selected", COLOR_BLACK, COLOR_WHITE);
    }

    // 绘制页码信息
    if (s_ble_state.current_book_id != 0) {
        char page_info[64];
        if (s_ble_state.total_pages > 0) {
            snprintf(page_info, sizeof(page_info), "Page %u / %u",
                     s_ble_state.current_page + 1, s_ble_state.total_pages);
        } else {
            snprintf(page_info, sizeof(page_info), "Page %u",
                     s_ble_state.current_page + 1);
        }
        display_draw_text_menu(20, SCREEN_HEIGHT - 60, page_info, COLOR_BLACK, COLOR_WHITE);
    }

    // 显示初始化确认提示
    if (s_ble_state.current_book_id != 0 && !s_ble_state.initialization_complete) {
        display_draw_text_menu(20, SCREEN_HEIGHT / 2 - 40,
                               "Click Confirm to start reading",
                               COLOR_BLACK, COLOR_WHITE);
        display_draw_text_menu(20, SCREEN_HEIGHT / 2,
                               "Press CONFIRM",
                               COLOR_BLACK, COLOR_WHITE);
        s_ble_state.showing_confirm_prompt = true;
    } else {
        s_ble_state.showing_confirm_prompt = false;
    }

    // 绘制底部提示
    if (!s_ble_state.initialization_complete && s_ble_state.current_book_id != 0) {
        display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                               "Confirm: Start",
                               COLOR_BLACK, COLOR_WHITE);
    } else {
        display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                               "Up: Prev  Down: Next  Back: Return",
                               COLOR_BLACK, COLOR_WHITE);
    }

    // 刷新墨水屏显示（使用全刷模式保证显示清晰）
    display_refresh(REFRESH_MODE_FULL);

    ESP_LOGI(TAG, "on_draw END");
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

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
                
                // 立即重绘显示该页（有缓存就显示，无缓存显示提示）
                screen->needs_redraw = true;
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
                
                // 立即重绘显示该页（有缓存就显示，无缓存显示提示）
                screen->needs_redraw = true;
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
                
                // 首先发送初始页码 0 给手机
                send_page_sync_notification(0);
                
                screen->needs_redraw = true;
            } else if (s_ble_state.state == BLE_READER_STATE_IDLE ||
                       s_ble_state.state == BLE_READER_STATE_SCANNING) {
                // 没有书籍时，扫描 BLE 设备
                ESP_LOGI(TAG, "Starting BLE scan...");
                s_ble_state.state = BLE_READER_STATE_SCANNING;
                ble_reader_screen_start_scan();
                screen->needs_redraw = true;
            }
            break;

        case BTN_BACK:
            // Back button
            if (s_ble_state.state == BLE_READER_STATE_SCANNING) {
                ble_reader_screen_stop_scan();
            }
            if (s_ble_state.device_connected) {
                ble_reader_screen_disconnect();
            }
            screen_manager_show("home");
            break;

        default:
            break;
    }

    screen->needs_redraw = true;
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
    ble_manager_register_device_found_cb(ble_device_found_callback);
    ble_manager_register_connect_cb(ble_connect_callback);
    ble_manager_register_data_received_cb(ble_data_received_callback);

    // 设备作为外设，开始广播等待手机连接
    s_ble_state.state = BLE_READER_STATE_IDLE;
    ESP_LOGI(TAG, "Waiting for phone connection (advertising as MFP-EPD)...");
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "BLE Reader screen hidden");

    // 清理蓝牙连接
    if (s_ble_state.device_connected) {
        ble_reader_screen_disconnect();
    }

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

void ble_reader_screen_start_scan(void)
{
    ESP_LOGI(TAG, "Starting BLE scan");
    s_ble_state.state = BLE_READER_STATE_SCANNING;
    ble_manager_start_scan(0);
}

void ble_reader_screen_stop_scan(void)
{
    ESP_LOGI(TAG, "Stopping BLE scan");

    if (s_ble_state.state == BLE_READER_STATE_SCANNING) {
        s_ble_state.state = BLE_READER_STATE_IDLE;
    }

    ble_manager_stop_scan();
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
