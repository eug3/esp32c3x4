#include "ble_reader.h"

// ...existing code...

static int32_t current_logical_index = 0;

// 收到翻页指令后，根据逻辑索引读取缓存的页面
void ble_reader_handle_next_page(void) {
    // 从逻辑索引 + 1 的槽位读取页面
    int next_index = current_logical_index + 1;
    int slot = ((next_index % 3) + 3) % 3;
    
    ESP_LOGI(TAG, "[NEXT] current=%d, next=%d, slot=%d", 
             current_logical_index, next_index, slot);
    
    // 读取 slot 中的页面并显示
    ble_reader_display_page(slot);
    current_logical_index = next_index;
}

// 返回刷新：重新显示当前页（不改变逻辑索引）
void ble_reader_refresh_current_page(void) {
    int slot = ((current_logical_index % 3) + 3) % 3;
    
    ESP_LOGI(TAG, "[REFRESH] Redrawing current page (logical_index=%d, slot=%d)", 
             current_logical_index, slot);
    
    // 从当前逻辑索引的槽位读取并重新显示
    ble_reader_display_page(slot);
}

// 接收新页面：写入对应的物理槽位
void ble_reader_handle_page_received(int32_t logical_index, const uint8_t *data, uint32_t length) {
    int slot = ((logical_index % 3) + 3) % 3;
    
    char filename[32];
    snprintf(filename, sizeof(filename), "slot%d", slot);
    
    ESP_LOGI(TAG, "[PAGE_RX] logical=%d → slot=%d", logical_index, slot);
    
    // 写入 LittleFS
    // ...存储逻辑...
}

// ...existing code...