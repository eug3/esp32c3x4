#include "reading_screen.h"
#include "ble_reader.h"
#include "input_event.h"
#include "esp_log.h"

static const char *TAG = "reading_screen";

static void reading_screen_handle_input(reading_screen_t *screen, input_event_t *event) {
    switch (event->button) {
        case BUTTON_RIGHT:
            ESP_LOGI(TAG, "Next chapter button pressed");
            // 发送下一章命令到 BLE 客户端
            ble_reader_send_next_chapter();
            break;
            
        case BUTTON_LEFT:
            ESP_LOGI(TAG, "Previous chapter button pressed");
            // 发送上一章命令到 BLE 客户端
            ble_reader_send_prev_chapter();
            break;
            
        // ...existing code...
    }
}

// ...existing code...