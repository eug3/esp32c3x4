/**
 * @file ble_reader_screen.c
 * @brief 蓝牙读书屏幕实现 - 分页位图显示，支持滑动窗口缓存
 */

#include "ble_reader_screen.h"
#include "ble_manager.h"
#include "ble_book_protocol.h"
#include "ble_cache_manager.h"
#include "display_engine.h"
#include "fonts.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

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
    
    // 位图缓冲区
    uint8_t *page_buffer;               // 当前页面的位图数据（48KB）
    bool page_loaded;                   // 当前页面是否已加载
    
    // 预加载状态
    bool preload_requested;             // 是否已请求预加载
    uint16_t preload_start_page;        // 预加载的起始页
} ble_reader_state_internal_t;

static ble_reader_state_internal_t s_ble_state = {
    .state = BLE_READER_STATE_IDLE,
    .current_book_id = 0,
    .current_page = 0,
    .total_pages = 0,
    .device_connected = false,
    .page_buffer = NULL,
    .page_loaded = false,
    .preload_requested = false,
    .preload_start_page = 0,
};

// 屏幕上下文
static screen_context_t *s_context = NULL;

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

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 初始化页面缓冲区
 */
static bool init_page_buffer(void)
{
    if (s_ble_state.page_buffer != NULL) {
        return true;
    }

    // 分配位图缓冲区（48KB）
    s_ble_state.page_buffer = (uint8_t *)heap_caps_malloc(BLE_BITMAP_SIZE, MALLOC_CAP_SPIRAM);
    if (s_ble_state.page_buffer == NULL) {
        s_ble_state.page_buffer = (uint8_t *)heap_caps_malloc(BLE_BITMAP_SIZE, MALLOC_CAP_8BIT);
    }

    if (s_ble_state.page_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate page buffer");
        return false;
    }

    ESP_LOGI(TAG, "Page buffer allocated: %u bytes", BLE_BITMAP_SIZE);
    return true;
}

/**
 * @brief 加载页面到显示缓冲区
 */
static bool load_current_page(void)
{
    if (s_ble_state.current_book_id == 0) {
        return false;
    }

    // 尝试从缓存加载
    uint32_t read_size = ble_cache_load_page(s_ble_state.current_book_id,
                                             s_ble_state.current_page,
                                             s_ble_state.page_buffer,
                                             BLE_BITMAP_SIZE);

    if (read_size == BLE_BITMAP_SIZE) {
        s_ble_state.page_loaded = true;
        
        // 更新阅读位置，触发预加载检查
        ble_cache_update_read_position(s_ble_state.current_book_id, 
                                       s_ble_state.current_page);
        return true;
    }

    ESP_LOGW(TAG, "Failed to load page: book=%04x, page=%u",
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

    // 动态 UUID 交换：只连接“明确广播了服务 UUID”的设备，避免误连。
    if (!device->has_service_uuid128) {
        return;
    }

    // 自动连接到强信号设备
    if (device->rssi > -70) {
        ESP_LOGI(TAG, "Attempting to connect to: %s", device->name);
        ble_manager_set_target_service_uuid128_le(device->service_uuid128_le);
        ble_reader_screen_connect_device(device->addr);
    }
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
    }
}

/**
 * @brief 蓝牙数据接收回调
 */
static void ble_data_received_callback(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        return;
    }

    // 通过协议解析器处理数据
    ble_pkt_type_t pkt_type = ble_book_protocol_parse(data, length);
    
    if (pkt_type == BLE_PKT_TYPE_DATA) {
        // 这是数据包，交由协议处理
        const ble_data_pkt_chunk_t *chunk = (const ble_data_pkt_chunk_t *)data;
        ble_book_protocol_handle_data_chunk(&chunk->header, chunk);
    } else if (pkt_type == BLE_PKT_TYPE_END) {
        // 无更多数据
        const ble_end_pkt_t *end_pkt = (const ble_end_pkt_t *)data;
        ESP_LOGI(TAG, "Received END packet: book=%04x, last_page=%u",
                 end_pkt->book_id, end_pkt->last_page);
        s_ble_state.total_pages = end_pkt->last_page + 1;
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
        case BLE_READER_STATE_RECEIVING:
            status_str = "Status: Receiving...";
            break;
        case BLE_READER_STATE_READING:
            status_str = "Status: Reading";
            break;
        default:
            status_str = "Status: Unknown";
            break;
    }
    display_draw_text_menu(20, status_y, status_str, COLOR_BLACK, COLOR_WHITE);

    // 绘制页面内容
    if (s_ble_state.page_loaded && s_ble_state.page_buffer != NULL) {
        // 位图数据应该是预格式化的原始像素数据（8位灰度或1位黑白）
        // 帧缓冲大小: 480 * 800 / 8 = 48000 字节（1位）或 480 * 800 = 384000（8位）
        // 位图大小: 48KB = 49152 字节
        // 假设位图是 1 位黑白格式，覆盖整个屏幕
        
        uint8_t *framebuffer = display_get_framebuffer();
        if (framebuffer != NULL) {
            // 直接拷贝位图数据到帧缓冲
            // 注意：实际映射取决于位图格式和硬件配置
            uint16_t copy_len = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;  // 1-bit format
            if (copy_len > BLE_BITMAP_SIZE) {
                copy_len = BLE_BITMAP_SIZE;
            }
            
            memcpy(framebuffer, s_ble_state.page_buffer, copy_len);
            ESP_LOGI(TAG, "Bitmap displayed, copied %u bytes", copy_len);
        }
    } else if (s_ble_state.current_book_id != 0) {
        // 页面未加载，显示提示
        display_draw_text_menu(20, 100, "Loading page...", COLOR_BLACK, COLOR_WHITE);
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

    // 绘制底部提示
    display_draw_text_menu(20, SCREEN_HEIGHT - 40,
                           "Up: Prev  Down: Next  Back: Return",
                           COLOR_BLACK, COLOR_WHITE);

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
            // 上一页
            if (s_ble_state.current_page > 0) {
                ble_reader_screen_prev_page();
            }
            break;

        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            // 下一页
            if (s_ble_state.total_pages == 0 || 
                s_ble_state.current_page < s_ble_state.total_pages - 1) {
                ble_reader_screen_next_page();
            }
            break;

        case BTN_CONFIRM:
            // 确认键 - 重新扫描或进行其他操作
            if (s_ble_state.state == BLE_READER_STATE_IDLE ||
                s_ble_state.state == BLE_READER_STATE_SCANNING) {
                ESP_LOGI(TAG, "Starting BLE scan...");
                s_ble_state.state = BLE_READER_STATE_SCANNING;
                ble_reader_screen_start_scan();
                screen->needs_redraw = true;
            }
            break;

        case BTN_BACK:
            // 返回键
            if (s_ble_state.state == BLE_READER_STATE_SCANNING) {
                ble_reader_screen_stop_scan();
            }
            if (s_ble_state.device_connected) {
                ble_reader_screen_disconnect();
            }
            screen_manager_show("home");
            break;

        default:
            return;
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

    // 初始化页面缓冲区（仅用于显示，不再用于BLE接收）
    if (!init_page_buffer()) {
        ESP_LOGE(TAG, "Failed to initialize page buffer");
        return;
    }

    // 初始化蓝牙协议（流式写入模式，无需外部缓冲）
    if (!ble_book_protocol_init()) {
        ESP_LOGE(TAG, "Failed to initialize BLE protocol");
        return;
    }

    // 注册协议回调
    ble_book_protocol_register_page_ready_cb(on_page_ready);
    ble_cache_register_preload_cb(on_preload_needed);

    // 初始化蓝牙管理器
    if (!ble_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize BLE manager");
        return;
    }

    // 注册蓝牙回调
    ble_manager_register_device_found_cb(ble_device_found_callback);
    ble_manager_register_connect_cb(ble_connect_callback);
    ble_manager_register_data_received_cb(ble_data_received_callback);

    // 开始扫描蓝牙设备
    s_ble_state.state = BLE_READER_STATE_SCANNING;
    ble_reader_screen_start_scan();
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "BLE Reader screen hidden");

    // 清理蓝牙
    if (s_ble_state.state == BLE_READER_STATE_SCANNING) {
        ble_reader_screen_stop_scan();
    }
    if (s_ble_state.device_connected) {
        ble_reader_screen_disconnect();
    }

    ble_manager_deinit();
    ble_book_protocol_deinit();

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
