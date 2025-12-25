#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "boot_selector";

#define UART_PORT UART_NUM_0
#define TIMEOUT_SECONDS 10
#define GPIO_BOOT_SELECT GPIO_NUM_9  // 可根据需要修改

typedef struct {
    const char *name;
    esp_partition_subtype_t subtype;
} app_info_t;

static const app_info_t apps[] = {
    {"app0", ESP_PARTITION_SUBTYPE_APP_OTA_0},
    {"app1", ESP_PARTITION_SUBTYPE_APP_OTA_1},
    {"app2", ESP_PARTITION_SUBTYPE_APP_OTA_2},
};

static const int num_apps = sizeof(apps) / sizeof(apps[0]);

void print_boot_menu()
{
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║     ESP32-C3 启动选择器 v1.0          ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n请选择要启动的应用程序：\n\n");
    
    for (int i = 0; i < num_apps; i++) {
        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, 
            apps[i].subtype, 
            apps[i].name
        );
        printf("  [%d] %s", i, apps[i].name);
        if (part) {
            printf(" (已找到，大小: 0x%lX)\n", part->size);
        } else {
            printf(" (未找到)\n");
        }
    }
    
    printf("\n  [r] 重新显示菜单\n");
    printf("  [s] 保存选择并设为默认\n");
    printf("\n");
    printf("倒计时 %d 秒后将启动默认应用...\n", TIMEOUT_SECONDS);
    printf("请按数字键选择: ");
    fflush(stdout);
}

int get_default_app_from_nvs()
{
    nvs_handle_t handle;
    int32_t default_app = 0;
    
    esp_err_t err = nvs_open("boot_sel", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_i32(handle, "default", &default_app);
        nvs_close(handle);
    }
    
    return (int)default_app;
}

void save_default_app_to_nvs(int app_index)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("boot_sel", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_i32(handle, "default", app_index);
        nvs_commit(handle);
        nvs_close(handle);
        printf("✓ 已保存 %s 为默认启动应用\n", apps[app_index].name);
    } else {
        printf("✗ 保存失败: %s\n", esp_err_to_name(err));
    }
}

void boot_to_app(int app_index, bool save_as_default)
{
    if (app_index < 0 || app_index >= num_apps) {
        printf("✗ 无效的应用索引: %d\n", app_index);
        return;
    }
    
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        apps[app_index].subtype,
        apps[app_index].name
    );
    
    if (!partition) {
        printf("✗ 未找到 %s 分区\n", apps[app_index].name);
        return;
    }
    
    if (save_as_default) {
        save_default_app_to_nvs(app_index);
    }
    
    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        printf("✗ 设置启动分区失败: %s\n", esp_err_to_name(err));
        return;
    }
    
    printf("\n正在启动 %s...\n", apps[app_index].name);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

int check_gpio_selection()
{
    // 配置 GPIO 为输入，带上拉
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_BOOT_SELECT),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);
    
    // 延迟以确保引脚稳定
    vTaskDelay(pdMS_TO_TICKS(10));
    
    int level = gpio_get_level(GPIO_BOOT_SELECT);
    
    // 如果 GPIO 为低电平（按钮按下），进入选择模式
    return (level == 0) ? -1 : get_default_app_from_nvs();
}

void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 配置 UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 512, 0, 0, NULL, 0));
    
    // 延迟一下让串口稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 检查 GPIO 是否要求进入选择模式
    int gpio_choice = check_gpio_selection();
    bool force_menu = (gpio_choice < 0);
    int default_app = force_menu ? 0 : gpio_choice;
    
    ESP_LOGI(TAG, "启动选择器已启动");
    ESP_LOGI(TAG, "GPIO 检查: %s", force_menu ? "强制菜单" : "自动启动");
    ESP_LOGI(TAG, "默认应用: %s (索引 %d)", apps[default_app].name, default_app);
    
    print_boot_menu();
    
    int countdown = TIMEOUT_SECONDS;
    int selected_app = default_app;
    bool save_default = false;
    bool user_interacted = false;
    
    while (countdown > 0 || user_interacted) {
        uint8_t data[32];
        int len = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(1000));
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char ch = (char)data[i];
                
                if (ch >= '0' && ch < '0' + num_apps) {
                    int choice = ch - '0';
                    selected_app = choice;
                    user_interacted = true;
                    printf("\n✓ 选择了 %s\n", apps[choice].name);
                    printf("按回车启动，或按 's' 保存为默认并启动\n");
                } else if (ch == 's' || ch == 'S') {
                    save_default = true;
                    printf("\n将保存 %s 为默认应用\n", apps[selected_app].name);
                    boot_to_app(selected_app, true);
                    return;
                } else if (ch == '\r' || ch == '\n') {
                    if (user_interacted) {
                        boot_to_app(selected_app, save_default);
                        return;
                    }
                } else if (ch == 'r' || ch == 'R') {
                    print_boot_menu();
                    countdown = TIMEOUT_SECONDS;
                    user_interacted = false;
                }
            }
        } else if (!user_interacted) {
            countdown--;
            if (countdown > 0) {
                printf("\r倒计时 %d 秒...  ", countdown);
                fflush(stdout);
            }
        }
    }
    
    // 超时，启动默认应用
    printf("\n\n超时，启动默认应用 %s\n", apps[selected_app].name);
    boot_to_app(selected_app, false);
}
