/**
 * @file ble_book_protocol.c
 * @brief 蓝牙图书阅读协议实现
 */

#include "ble_book_protocol.h"
#include "ble_cache_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BLE_PROTOCOL";

// 协议状态
typedef struct {
    bool initialized;
    ble_page_ready_cb page_ready_cb;
    
    // 当前接收状态
    uint16_t current_book_id;
    uint16_t current_page_num;
    uint32_t received_bytes;
    uint8_t *receive_buffer;  // 接收位图数据的临时缓冲区
} ble_protocol_state_t;

static ble_protocol_state_t s_protocol_state = {
    .initialized = false,
    .page_ready_cb = NULL,
    .current_book_id = 0,
    .current_page_num = 0,
    .received_bytes = 0,
    .receive_buffer = NULL,
};

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 初始化接收缓冲区
 */
static bool init_receive_buffer(void)
{
    if (s_protocol_state.receive_buffer != NULL) {
        return true;  // 已初始化
    }

    // 分配接收缓冲区
    s_protocol_state.receive_buffer = (uint8_t *)heap_caps_malloc(BLE_BITMAP_SIZE, MALLOC_CAP_SPIRAM);
    if (s_protocol_state.receive_buffer == NULL) {
        s_protocol_state.receive_buffer = (uint8_t *)heap_caps_malloc(BLE_BITMAP_SIZE, MALLOC_CAP_8BIT);
    }

    if (s_protocol_state.receive_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate receive buffer (%u bytes)", BLE_BITMAP_SIZE);
        return false;
    }

    ESP_LOGI(TAG, "Receive buffer allocated: %u bytes", BLE_BITMAP_SIZE);
    return true;
}

/**
 * @brief 清空接收缓冲区
 */
static void clear_receive_buffer(void)
{
    s_protocol_state.received_bytes = 0;
    s_protocol_state.current_book_id = 0;
    s_protocol_state.current_page_num = 0;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool ble_book_protocol_init(void)
{
    if (s_protocol_state.initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE book protocol...");

    // 初始化缓存管理器
    if (!ble_cache_init()) {
        ESP_LOGE(TAG, "Failed to initialize cache manager");
        return false;
    }

    // 初始化接收缓冲区
    if (!init_receive_buffer()) {
        ble_cache_deinit();
        return false;
    }

    s_protocol_state.initialized = true;
    ESP_LOGI(TAG, "BLE book protocol initialized");
    return true;
}

void ble_book_protocol_deinit(void)
{
    if (!s_protocol_state.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing BLE book protocol");

    // 清理接收缓冲区
    if (s_protocol_state.receive_buffer != NULL) {
        heap_caps_free(s_protocol_state.receive_buffer);
        s_protocol_state.receive_buffer = NULL;
    }

    // 清理缓存管理器
    ble_cache_deinit();

    clear_receive_buffer();
    s_protocol_state.initialized = false;
}

ble_pkt_type_t ble_book_protocol_parse(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length < 1) {
        return BLE_PKT_TYPE_ERROR;
    }

    return (ble_pkt_type_t)data[0];
}

uint16_t ble_book_protocol_make_request(uint16_t book_id, uint16_t start_page, 
                                        uint8_t page_count, uint8_t *buffer, 
                                        uint16_t buffer_size)
{
    if (buffer == NULL || buffer_size < BLE_REQUEST_PKT_SIZE) {
        return 0;
    }

    // 限制请求页数
    if (page_count > 5) {
        page_count = 5;
    }

    ble_request_pkt_t *pkt = (ble_request_pkt_t *)buffer;
    pkt->type = BLE_PKT_TYPE_REQUEST;
    pkt->book_id = book_id;
    pkt->start_page = start_page;
    pkt->page_count = page_count;
    pkt->reserved = 0;

    ESP_LOGI(TAG, "Request packet created: book=%04x, pages=%u-%u, count=%u",
             book_id, start_page, start_page + page_count - 1, page_count);

    return BLE_REQUEST_PKT_SIZE;
}

bool ble_book_protocol_handle_data_chunk(const ble_data_pkt_header_t *header,
                                         const ble_data_pkt_chunk_t *chunk)
{
    if (!s_protocol_state.initialized || header == NULL || chunk == NULL) {
        return false;
    }

    // 检查是否是新的页面
    if (header->page_num != s_protocol_state.current_page_num ||
        header->book_id != s_protocol_state.current_book_id) {
        
        // 如果有前一个页面未完成，保存它
        if (s_protocol_state.received_bytes > 0) {
            ESP_LOGW(TAG, "Previous page not fully received, received=%u bytes",
                     s_protocol_state.received_bytes);
        }

        // 重置接收状态
        clear_receive_buffer();
        s_protocol_state.current_book_id = header->book_id;
        s_protocol_state.current_page_num = header->page_num;
        ESP_LOGI(TAG, "Starting new page reception: book=%04x, page=%u",
                 header->book_id, header->page_num);
    }

    // 验证数据块的偏移和大小
    if (chunk->offset != s_protocol_state.received_bytes) {
        ESP_LOGE(TAG, "Data offset mismatch: expected=%u, got=%u",
                 s_protocol_state.received_bytes, chunk->offset);
        return false;
    }

    if (chunk->chunk_size > BLE_DATA_CHUNK_DATA_SIZE) {
        ESP_LOGE(TAG, "Invalid chunk size: %u", chunk->chunk_size);
        return false;
    }

    // 检查缓冲区是否足够
    if (s_protocol_state.received_bytes + chunk->chunk_size > BLE_BITMAP_SIZE) {
        ESP_LOGE(TAG, "Receive buffer overflow");
        return false;
    }

    // 复制数据到接收缓冲区
    memcpy(s_protocol_state.receive_buffer + chunk->offset,
           chunk->data, chunk->chunk_size);
    s_protocol_state.received_bytes += chunk->chunk_size;

    ESP_LOGD(TAG, "Received chunk: offset=%u, size=%u, total=%u/%u",
             chunk->offset, chunk->chunk_size,
             s_protocol_state.received_bytes, BLE_BITMAP_SIZE);

    // 检查是否接收完整
    if (s_protocol_state.received_bytes >= BLE_BITMAP_SIZE) {
        // 完整页面已接收，保存到缓存
        if (ble_cache_save_page(header->book_id, header->page_num,
                                s_protocol_state.receive_buffer,
                                BLE_BITMAP_SIZE)) {
            ESP_LOGI(TAG, "Page saved to cache: book=%04x, page=%u",
                     header->book_id, header->page_num);

            // 触发页面就绪回调
            if (s_protocol_state.page_ready_cb != NULL) {
                s_protocol_state.page_ready_cb(header->book_id, header->page_num);
            }

            // 清空接收缓冲区，准备接收下一页
            clear_receive_buffer();
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to save page to cache");
            clear_receive_buffer();
            return false;
        }
    }

    return true;  // 继续接收
}

void ble_book_protocol_register_page_ready_cb(ble_page_ready_cb cb)
{
    s_protocol_state.page_ready_cb = cb;
}
