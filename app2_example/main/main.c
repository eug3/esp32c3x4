#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_err.h"
#include "driver/uart.h"

#define UART_PORT UART_NUM_0

void print_menu()
{
    printf("--- app2_example ---\n");
    printf("按 2: 将下次启动设置为 app2 并重启\n");
    printf("按 r: 继续当前应用（本示例）\n");
}

void boot_to_app2()
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, "app2");
    if (!p) {
        printf("app2 分区未找到\n");
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        printf("esp_ota_set_boot_partition failed: %d\n", err);
        return;
    }
    printf("设置成功，重启以从 app2 启动...\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

void app_main(void)
{
    // 初始化 NVS（OTA API 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 简单 UART 配置（如果需要）
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_PORT, &uart_config);
    uart_driver_install(UART_PORT, 256, 0, 0, NULL, 0);

    print_menu();

    while (1) {
        // 读取一个字符（阻塞）
        uint8_t ch;
        int len = uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            if (ch == '2') {
                boot_to_app2();
            } else if (ch == 'r') {
                printf("继续运行当前应用...\n");
            } else {
                printf("未知命令: %c\n", ch);
                print_menu();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
