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
    
    // 当前接收状态（流式写入）
    uint16_t current_book_id;
    uint16_t current_page_num;
    uint32_t received_bytes;
    FILE *current_file;       // 当前写入的文件句柄
    char current_filename[64]; // 当前写入的文件路径
} ble_protocol_state_t;

static ble_protocol_state_t s_protocol_state = {
    .initialized = false,
    .page_ready_cb = NULL,
    .current_book_id = 0,
    .current_page_num = 0,
    .received_bytes = 0,
    .current_file = NULL,
    .current_filename = {0},
};

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 清空接收状态并关闭文件
 */
static void clear_receive_state(void)
{
    if (s_protocol_state.current_file != NULL) {
        fclose(s_protocol_state.current_file);
        s_protocol_state.current_file = NULL;
    }
    s_protocol_state.received_bytes = 0;
    s_protocol_state.current_book_id = 0;
    s_protocol_state.current_page_num = 0;
    memset(s_protocol_state.current_filename, 0, sizeof(s_protocol_state.current_filename));
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool ble_book_protocol_init(void)
{
    if (s_protocol_state.initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE book protocol (stream mode)...");

    // 初始化缓存管理器
    if (!ble_cache_init()) {
        ESP_LOGE(TAG, "Failed to initialize cache manager");
        return false;
    }

    s_protocol_state.initialized = true;
    ESP_LOGI(TAG, "BLE book protocol initialized (stream mode)");
    return true;
}

void ble_book_protocol_deinit(void)
{
    if (!s_protocol_state.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing BLE book protocol");

    // 关闭可能打开的文件
    clear_receive_state();

    // 清理缓存管理器
    ble_cache_deinit();

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
        
        // 关闭前一个文件（如果有）
        if (s_protocol_state.current_file != NULL) {
            ESP_LOGW(TAG, "Previous page not fully received, closing file");
            fclose(s_protocol_state.current_file);
            s_protocol_state.current_file = NULL;
        }

        // 生成新文件路径
        char filename[64];
        snprintf(filename, sizeof(filename), "/littlefs/ble_pages/book_%04x_page_%05u.bin",
                 header->book_id, header->page_num);
        
        // 打开文件准备写入
        s_protocol_state.current_file = fopen(filename, "wb");
        if (s_protocol_state.current_file == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
            return false;
        }
        
        // 更新状态
        s_protocol_state.current_book_id = header->book_id;
        s_protocol_state.current_page_num = header->page_num;
        s_protocol_state.received_bytes = 0;
        strncpy(s_protocol_state.current_filename, filename, sizeof(s_protocol_state.current_filename) - 1);
        
        ESP_LOGI(TAG, "Starting stream write: book=%04x, page=%u -> %s",
                 header->book_id, header->page_num, filename);
    }

    // 验证数据块的偏移
    if (chunk->offset != s_protocol_state.received_bytes) {
        ESP_LOGE(TAG, "Data offset mismatch: expected=%u, got=%u",
                 s_protocol_state.received_bytes, chunk->offset);
        clear_receive_state();
        return false;
    }

    if (chunk->chunk_size > BLE_DATA_CHUNK_DATA_SIZE) {
        ESP_LOGE(TAG, "Invalid chunk size: %u", chunk->chunk_size);
        clear_receive_state();
        return false;
    }

    // 检查文件句柄
    if (s_protocol_state.current_file == NULL) {
        ESP_LOGE(TAG, "File handle is NULL");
        return false;
    }

    // 流式写入数据到文件
    size_t written = fwrite(chunk->data, 1, chunk->chunk_size, s_protocol_state.current_file);
    if (written != chunk->chunk_size) {
        ESP_LOGE(TAG, "File write failed: expected=%u, written=%u", chunk->chunk_size, written);
        clear_receive_state();
        return false;
    }
    
    s_protocol_state.received_bytes += chunk->chunk_size;

    ESP_LOGD(TAG, "Streamed chunk: offset=%u, size=%u, total=%u/%u",
             chunk->offset, chunk->chunk_size,
             s_protocol_state.received_bytes, BLE_BITMAP_SIZE);

    // 检查是否接收完整
    if (s_protocol_state.received_bytes >= BLE_BITMAP_SIZE) {
        // 关闭文件
        fclose(s_protocol_state.current_file);
        s_protocol_state.current_file = NULL;
        
        ESP_LOGI(TAG, "Page stream complete: book=%04x, page=%u (%u bytes)",
                 s_protocol_state.current_book_id, s_protocol_state.current_page_num,
                 s_protocol_state.received_bytes);

        // 触发页面就绪回调
        if (s_protocol_state.page_ready_cb != NULL) {
            s_protocol_state.page_ready_cb(s_protocol_state.current_book_id,
                                          s_protocol_state.current_page_num);
        }

        // 清空状态，准备接收下一页
        clear_receive_state();
        return true;
    }

    return true;  // 继续接收
}

void ble_book_protocol_register_page_ready_cb(ble_page_ready_cb cb)
{
    s_protocol_state.page_ready_cb = cb;
}
