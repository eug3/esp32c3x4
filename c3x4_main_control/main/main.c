#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "DEV_Config.h"
#include "EPD_4in26.h"
#include "ImageData.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_cali.h"
#include "version.h"       // 自动生成的版本信息
#include "display_engine.h"
#include "screen_manager.h"
#include "home_screen.h"
#include "settings_screen_simple.h"
#include "file_browser_screen.h"
#include "image_viewer_screen.h"
#include "reader_screen_simple.h"
#include "input_handler.h"

// ============================================================================
// Xteink X4 引脚定义 - 参考 examples/xteink-x4-sample
// ============================================================================

// 按钮引脚 (ADC电阻分压方案)
#define BTN_GPIO1      GPIO_NUM_1  // 4个按钮: Back, Confirm, Left, Right
#define BTN_GPIO2      GPIO_NUM_2  // 2个按钮: Volume Up, Volume Down
#define BTN_GPIO3      GPIO_NUM_3  // 电源按钮 (数字输入)

// 电池和USB检测
#define BAT_GPIO0      GPIO_NUM_0  // 电池电压检测
#define UART0_RXD      GPIO_NUM_20 // USB连接检测 (HIGH = USB已连接)

// 按钮ADC阈值
#define BTN_THRESHOLD           100    // 阈值容差
#define BTN_RIGHT_VAL           3      // Right按钮ADC值
#define BTN_LEFT_VAL            1470   // Left按钮ADC值
#define BTN_CONFIRM_VAL         2655   // Confirm按钮ADC值
#define BTN_BACK_VAL            3470   // Back按钮ADC值
#define BTN_VOLUME_DOWN_VAL     3      // Volume Down按钮ADC值
#define BTN_VOLUME_UP_VAL       2205   // Volume Up按钮ADC值

// 电源按钮时间定义
#define POWER_BUTTON_WAKEUP_MS    1000  // 从睡眠唤醒需要按下时间
#define POWER_BUTTON_SLEEP_MS     1000  // 进入睡眠需要按下时间

// 电池监测 - ESP-IDF 6.1 新 API
adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = true;

// 按钮状态
static volatile button_t current_pressed_button = BTN_NONE;

// Define driver selection
#define TEST_DRIVER_SSD1677 1
#define TEST_DRIVER_GDEQ0426T82 2
#define TEST_DRIVER_SSD1681 3

#define CURRENT_DRIVER TEST_DRIVER_SSD1681  // Change this to test different drivers

// Forward declarations
esp_err_t sd_card_init(void);
void sd_card_test_read_write(const char *mount_point);

#define SDCARD_MOUNT_POINT "/sdcard"
#define SPI_DMA_CHAN    1

// SD card pins - Xteink X4 (与 EPD 共用 SPI 总线)
#define PIN_NUM_MISO  GPIO_NUM_7
#define PIN_NUM_MOSI  GPIO_NUM_10
#define PIN_NUM_CLK   GPIO_NUM_8
#define PIN_NUM_CS    GPIO_NUM_12   // SD_CS 专用引脚

// ============================================================================
// Xteink X4 按钮和电池功能函数
// ============================================================================

// 获取按钮名称字符串
__attribute__((unused))
static const char* get_button_name(button_t btn) {
    switch (btn) {
        case BTN_NONE:        return "None";
        case BTN_RIGHT:       return "RIGHT";
        case BTN_LEFT:        return "LEFT";
        case BTN_CONFIRM:     return "CONFIRM";
        case BTN_BACK:        return "BACK";
        case BTN_VOLUME_UP:   return "VOLUME_UP";
        case BTN_VOLUME_DOWN: return "VOLUME_DOWN";
        case BTN_POWER:       return "POWER";
        default:              return "Unknown";
    }
}

// 读取当前按下的按钮 (ADC电阻分压方案) - ESP-IDF 6.1
// 注意：此函数被外部调用，所以不是 static
button_t get_pressed_button(void) {
    int btn1_adc, btn2_adc;
    int btn1 = 0, btn2 = 0;

    // 读取ADC值多次取平均
    for (int i = 0; i < 3; i++) {
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &btn1_adc); // GPIO1
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &btn2_adc); // GPIO2
        btn1 += btn1_adc;
        btn2 += btn2_adc;
    }
    btn1 /= 3;
    btn2 /= 3;

    // 调试：记录上一次的按钮状态
    static button_t last_btn = BTN_NONE;

    button_t detected_btn = BTN_NONE;  // 预先检测按钮

    // 检查电源按钮 (数字输入)
    if (gpio_get_level(BTN_GPIO3) == 0) {
        detected_btn = BTN_POWER;
    }
    // 检查 BTN_GPIO1 (4个按钮通过电阻分压)
    else if (btn1 < BTN_RIGHT_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_RIGHT;
    } else if (btn1 < BTN_LEFT_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_LEFT;
    } else if (btn1 < BTN_CONFIRM_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_CONFIRM;
    } else if (btn1 < BTN_BACK_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_BACK;
    }
    // 检查 BTN_GPIO2 (2个按钮通过电阻分压)
    else if (btn2 < BTN_VOLUME_DOWN_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_VOLUME_DOWN;
    } else if (btn2 < BTN_VOLUME_UP_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_VOLUME_UP;
    }

    // 只在按钮变化时打印（调试用）
    if (detected_btn != last_btn) {
        ESP_LOGD("BTN_ADC", "GPIO1=%4d, GPIO2=%4d | Detected: %d (%s)",
                 btn1, btn2, detected_btn, get_button_name(detected_btn));
        last_btn = detected_btn;
    }

    return detected_btn;
}

// 初始化按钮和ADC - ESP-IDF 6.1
static void buttons_adc_init(void) {
    ESP_LOGI("BTN", "Initializing buttons and ADC...");

    // 配置 ADC Oneshot 模式
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 配置 ADC 通道
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // ESP-IDF 6.1 使用 DB_12 而不是 DB_11
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config)); // GPIO0 - 电池
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &config)); // GPIO1 - 按钮1
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config)); // GPIO2 - 按钮2

    // ADC 校准
    if (do_calibration) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
        if (ret == ESP_OK) {
            ESP_LOGI("BTN", "ADC calibration enabled");
        } else {
            ESP_LOGW("BTN", "ADC calibration failed, skipping");
            do_calibration = false;
        }
    }

    // 配置按钮引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_GPIO3),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 配置电池检测引脚
    gpio_set_direction(BAT_GPIO0, GPIO_MODE_INPUT);

    // 配置USB检测引脚
    gpio_set_direction(UART0_RXD, GPIO_MODE_INPUT);

    ESP_LOGI("BTN", "Buttons and ADC initialized");
}

// 检查是否正在充电 (USB连接检测)
static bool is_charging(void) {
    return gpio_get_level(UART0_RXD) == 1;
}

// 读取电池电压 (mV) - ESP-IDF 6.1
static uint32_t read_battery_voltage_mv(void) {
    int adc_raw = 0;
    int adc_result;
    for (int i = 0; i < 10; i++) {
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_result);
        adc_raw += adc_result;
    }
    adc_raw /= 10;

    int voltage = adc_raw;
    if (do_calibration && adc1_cali_handle) {
        adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage);
    } else {
        // 未校准时的近似值: ADC值 * 1.1mV * 2 (分压系数)
        voltage = (adc_raw * 1100) / 2048 * 2;
    }

    // 分压系数: Xteink X4 使用电阻分压，实际电压需要乘以系数
    // 通常分压比为 2:1，所以实际电压 = ADC电压 * 2
    return (uint32_t)voltage * 2;
}

// 读取电池百分比
static uint8_t read_battery_percentage(void) {
    uint32_t voltage_mv = read_battery_voltage_mv();
    // 简单线性映射: 3.0V = 0%, 4.2V = 100%
    if (voltage_mv < 3000) return 0;
    if (voltage_mv > 4200) return 100;
    return (voltage_mv - 3000) * 100 / (4200 - 3000);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI("WIFI", "WiFi station started, connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WIFI", "WiFi disconnected, retrying to connect to the AP");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WIFI", "WiFi connected successfully!");
        ESP_LOGI("WIFI", "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// WiFi initialization function
// 注意：在 4 灰度模式下帧缓冲更大（96KB），WiFi 初始化可能因内存不足失败。
// 不要 ESP_ERROR_CHECK 直接 abort，改为返回错误让系统继续运行。
esp_err_t wifi_init_sta(void)
{
    ESP_LOGI("WIFI", "Initializing WiFi in station mode...");
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WIFI", "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WIFI", "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_netif_create_default_wifi_sta();

    // 优化WiFi配置以减少内存使用(ESP32-C3只有100KB RAM)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // 减少RX缓冲区(默认10→4,节省约6KB)
    cfg.static_rx_buf_num = 4;     // 从默认10减到4
    cfg.dynamic_rx_buf_num = 8;    // 从默认32减到8
    cfg.dynamic_tx_buf_num = 8;    // 从默认32减到8
    cfg.tx_buf_type = 1;           // 使用动态分配
    cfg.cache_tx_buf_num = 1;      // 减少缓存
    
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &wifi_event_handler,
                                              NULL,
                                              &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "register WIFI_EVENT handler failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler,
                                              NULL,
                                              &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "register IP_EVENT handler failed: %s", esp_err_to_name(err));
        return err;
    }

    // Simple WiFi config — updated with requested credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "foxwifi-plus",
            .password = "epdc1984",
        },
    };
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI("WIFI", "WiFi initialization completed. Connecting...");
    return ESP_OK;
}

// SD card initialization function
esp_err_t sd_card_init(void)
{
    ESP_LOGI("SD", "Initializing SD card");

    esp_err_t ret;

    // Options for mounting the FAT filesystem on SD card.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char sd_mount_point[] = SDCARD_MOUNT_POINT;

    ESP_LOGI("SD", "Initializing SD card using SPI peripheral");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400;  // 降低到 400kHz 提高兼容性（原 1000kHz）

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // SPI 总线已在 DEV_Module_Init() 中初始化，尝试添加设备
    ESP_LOGI("SD", "Attaching SD card to existing SPI bus");

    ESP_LOGI("SD", "Mounting FAT filesystem at %s", sd_mount_point);
    
    // 尝试挂载，最多重试 3 次
    int retry_count = 0;
    const int max_retries = 3;
    
    while (retry_count < max_retries) {
        ret = esp_vfs_fat_sdspi_mount(sd_mount_point, &host, &slot_config, &mount_config, &card);
        
        if (ret == ESP_OK) {
            break;
        }
        
        retry_count++;
        ESP_LOGW("SD", "Mount attempt %d/%d failed: %s", retry_count, max_retries, esp_err_to_name(ret));
        
        if (retry_count < max_retries) {
            vTaskDelay(pdMS_TO_TICKS(500));  // 等待 500ms 后重试
        }
    }

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SD", "Failed to mount filesystem after %d attempts.", max_retries);
            ESP_LOGE("SD", "Possible reasons: 1) No SD card inserted, 2) Card not formatted as FAT");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE("SD", "SD card communication timeout after %d attempts.", max_retries);
            ESP_LOGE("SD", "Possible reasons: 1) SD card not inserted, 2) Poor connection, 3) Incompatible card");
        } else {
            ESP_LOGE("SD", "Failed to initialize the card (%s) after %d attempts.", esp_err_to_name(ret), max_retries);
        }
        ESP_LOGW("SD", "System will continue without SD card functionality");
        return ret;
    }

    ESP_LOGI("SD", "FAT filesystem mounted at %s", sd_mount_point);
    sdmmc_card_print_info(stdout, card);

    // Create a test file on SD card
    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "%s/test.txt", sd_mount_point);
    FILE *f = fopen(path_buf, "w");
    if (f != NULL) {
        fprintf(f, "SD card initialized successfully!\n");
        fclose(f);
        ESP_LOGI("SD", "Test file created on SD card");
    } else {
        ESP_LOGE("SD", "Failed to create test file on SD card");
    }

    // Test SD card read/write functionality
    sd_card_test_read_write(sd_mount_point);

    ESP_LOGI("SD", "SD card initialization completed");
    return ESP_OK;
}

// SD card read/write test function
void sd_card_test_read_write(const char *mount_point)
{
    ESP_LOGI("SD", "Testing SD card read/write functionality");

    // Test writing data
    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "%s/test_data.bin", mount_point);
    FILE *f = fopen(path_buf, "wb");
    if (f != NULL) {
        uint8_t test_data[256];
        for (int i = 0; i < 256; i++) {
            test_data[i] = i;
        }

        size_t written = fwrite(test_data, 1, sizeof(test_data), f);
        fclose(f);

        if (written == sizeof(test_data)) {
            ESP_LOGI("SD", "Write test successful: %d bytes written", written);
        } else {
            ESP_LOGE("SD", "Write test failed: only %d bytes written", written);
            return;
        }
    } else {
        ESP_LOGE("SD", "Failed to open file for writing");
        return;
    }

    // Test reading data
    f = fopen(path_buf, "rb");
    if (f != NULL) {
        uint8_t read_data[256];
        size_t read = fread(read_data, 1, sizeof(read_data), f);
        fclose(f);

        if (read == sizeof(read_data)) {
            bool data_ok = true;
            for (int i = 0; i < 256; i++) {
                if (read_data[i] != (uint8_t)i) {
                    data_ok = false;
                    break;
                }
            }

            if (data_ok) {
                ESP_LOGI("SD", "Read test successful: %d bytes read, data verified", read);
            } else {
                ESP_LOGE("SD", "Read test failed: data verification failed");
            }
        } else {
            ESP_LOGE("SD", "Read test failed: only %d bytes read", read);
        }
    } else {
        ESP_LOGE("SD", "Failed to open file for reading");
    }

    // Test directory operations
    struct stat st;
    if (stat(path_buf, &st) == 0) {
        ESP_LOGI("SD", "File size: %ld bytes", st.st_size);
    }

    ESP_LOGI("SD", "SD card test completed");
}

// Save received image to SD card


static void ui_button_event_cb(button_t btn, button_event_t event, void *user_data)
{
    (void)user_data;
    screen_manager_handle_event(btn, event);
}

static void input_poll_task(void *arg)
{
    (void)arg;
    while (1) {
        input_handler_poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


void app_main(void)
{
    printf("ESP32 BLE and WiFi System Starting...\n");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI("MAIN", "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // ============================================================================
    // Xteink X4: 先初始化按钮和ADC（因为欢迎页面需要读取电池信息）
    // ============================================================================
    ESP_LOGI("MAIN", "Initializing Xteink X4 button system and battery monitoring...");
    buttons_adc_init();
    ESP_LOGI("BAT", "Battery: %lu mV, %u%%, Charging: %s",
             read_battery_voltage_mv(),
             read_battery_percentage(),
             is_charging() ? "Yes" : "No");

    // ============================================================================
    // Xteink X4: 初始化 EPD 墨水屏
    // ============================================================================
    ESP_LOGI("MAIN", "Initializing EPD...");

    // 初始化 SPI 和 e-Paper (在 BLE/WiFi 之前初始化以避免电源问题)
    DEV_Module_Init();

    // 初始化 EPD 硬件（使用快刷模式以避免后续刷新时变黑）
    EPD_4in26_Init_Fast();
    EPD_4in26_Clear_Fast();

    // Add delay before initializing high-power components to prevent brownout
    ESP_LOGI("MAIN", "Waiting 1 second before initializing SD card to prevent brownout...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Initialize SD card
    ESP_LOGI("MAIN", "Initializing SD card...");
    esp_err_t sd_ret = sd_card_init();
    if (sd_ret != ESP_OK) {
        ESP_LOGW("MAIN", "SD card initialization failed, but system will continue");
        ESP_LOGW("MAIN", "File browser and SD-related features will be unavailable");
    }

    // ============================================================================
    // Xteink X4: 初始化显示引擎和屏幕管理器
    // ============================================================================

    // 创建屏幕上下文
    screen_context_t context = {0};
    context.battery_pct = read_battery_percentage();
    context.version_str = VERSION_STRING;

    // 初始化显示引擎
    ESP_LOGI("MAIN", "Initializing display engine...");
    display_config_t disp_config = {
        .use_partial_refresh = true,
        .auto_refresh = false,
        .default_mode = REFRESH_MODE_PARTIAL,
    };
    if (!display_engine_init(&disp_config)) {
        ESP_LOGE("MAIN", "Display engine init failed!");
    }

    // 初始化屏幕管理器
    ESP_LOGI("MAIN", "Initializing screen manager...");
    screen_manager_init(&context);

    // 注册屏幕
    home_screen_init();
    settings_screen_simple_init();
    file_browser_screen_init();
    image_viewer_screen_init();  // 初始化图片浏览器
    reader_screen_init();  // 初始化阅读器
    screen_manager_register(home_screen_get_instance());
    screen_manager_register(settings_screen_simple_get_instance());
    screen_manager_register(file_browser_screen_get_instance());
    screen_manager_register(image_viewer_screen_get_instance());  // 注册图片浏览器
    screen_manager_register(reader_screen_get_instance());  // 注册阅读器

    // 显示主屏幕
    ESP_LOGI("MAIN", "Showing home screen...");
    screen_manager_show("home");

    // ============================================================================
    // Xteink X4: 初始化按键输入处理（轮询 + 事件派发到当前屏幕）
    // ============================================================================
    input_handler_init(NULL);
    input_handler_register_callback(ui_button_event_cb, NULL);
    // 注意：input_poll 里可能触发屏幕切换/目录扫描等较深调用栈，给更大的栈以避免 stack protection fault
    xTaskCreate(input_poll_task, "input_poll", 8192, NULL, 5, NULL);

    ESP_LOGI("MAIN", "System initialized successfully.");
}

