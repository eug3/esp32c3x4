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
#include "esp_bt.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "DEV_Config.h"
#include "EPD_4in26.h"
#include "ImageData.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_cali.h"
#include "lvgl_driver.h"  // LVGL驱动适配层
#include "lvgl_demo.h"     // LVGL示例UI
#include "version.h"       // 自动生成的版本信息

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

// 按钮枚举定义在 lvgl_driver.h 中

// 电源按钮时间定义
#define POWER_BUTTON_WAKEUP_MS    1000  // 从睡眠唤醒需要按下时间
#define POWER_BUTTON_SLEEP_MS     1000  // 进入睡眠需要按下时间

// 电池监测 - ESP-IDF 6.1 新 API
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = true;

// BLE connection state management
static bool ble_connected = false;
static bool ble_advertising = false;
static uint16_t ble_conn_handle = 0;
static char ble_peer_addr[18] = {0};
static bool ble_pending_connection = false;
static char ble_local_addr[18] = {0};
static uint8_t own_addr_type;

// 按钮状态（保留 current_pressed_button 供未来使用）
static volatile button_t current_pressed_button = BTN_NONE;

// Define driver selection
#define TEST_DRIVER_SSD1677 1
#define TEST_DRIVER_GDEQ0426T82 2
#define TEST_DRIVER_SSD1681 3

#define CURRENT_DRIVER TEST_DRIVER_SSD1681  // Change this to test different drivers

// Forward declarations
static int start_advertising(void);
esp_err_t sd_card_init(void);
void sd_card_test_read_write(const char *mount_point);

#define SDCARD_MOUNT_POINT "/sdcard"
#define SPI_DMA_CHAN    1

// SD card pins - Xteink X4 (与 EPD 共用 SPI 总线)
#define PIN_NUM_MISO  GPIO_NUM_7
#define PIN_NUM_MOSI  GPIO_NUM_10
#define PIN_NUM_CLK   GPIO_NUM_8
#define PIN_NUM_CS    GPIO_NUM_12   // SD_CS 专用引脚

// BLE initialization function — use esp_nimble_hci_and_controller_init() to avoid
// controller state conflicts on ESP32-C3
#define DEVICE_NAME "ESP32-BLE"

static const char *BLE_TAG = "BLE_MIN";

// GATT service for receiving image data from mobile device
#define IMAGE_SERVICE_UUID     0x1234
#define IMAGE_DATA_CHAR_UUID   0x5678
// GATT characteristic (ESP32 -> phone) for page control commands (notify ASCII: "prev"/"next"/"capture")
#define CONTROL_CMD_CHAR_UUID  0x5679

// Frame protocol (written by phone to 0x5678):
// 0..3  : ASCII 'X4IM'
// 4     : version = 1
// 5     : format  = 1 (RGB565 little-endian)
// 6..7  : reserved
// 8..11 : payload length (uint32 LE)
#define X4IM_HDR_LEN 12

// JSON layout protocol:
// 0..3  : ASCII 'X4JS'
// 4     : version = 1
// 5..7  : reserved
// 8..11 : payload length (uint32 LE)
#define X4JS_HDR_LEN 12

// Image data storage - using external storage for large images
// static uint8_t image_data[160 * 120 * 2]; // Removed to save DRAM
static uint32_t image_data_len = 0;
static uint32_t image_expected_len = (480u * 800u * 2u);
static bool image_data_ready = false;
static uint32_t image_frame_id = 0;
static char current_image_filename[64] = {0};
static FILE *image_file = NULL;

// JSON layout storage
static char current_json_filename[64] = {0};
static FILE *json_file = NULL;
static uint32_t json_data_len = 0;
static uint32_t json_expected_len = 0;
static bool json_data_ready = false;

static uint16_t control_cmd_chr_val_handle = 0;
static char last_control_cmd[16] = {0};
static bool cmd_notify_enabled = false;

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
// 注意：此函数被lvgl_driver.c调用，所以不能是static
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

static uint32_t read_le_u32(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int control_cmd_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    // This characteristic is primarily NOTIFY (ESP32 -> phone). We allow READ so the phone
    // can verify the channel and fetch the last command for debugging.
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, last_control_cmd, strlen(last_control_cmd));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

// GATT characteristic access functions
static int image_data_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // Return a small status header instead of dumping the full image buffer over GATT.
        // Layout (13 bytes):
        // [0]    ready(1)/pending(0)
        // [1..4] received_len (uint32 LE)
        // [5..8] expected_len (uint32 LE)
        // [9..12] frame_id (uint32 LE)
        {
            uint8_t status[13];
            status[0] = image_data_ready ? 1 : 0;
            status[1] = (uint8_t)(image_data_len & 0xFF);
            status[2] = (uint8_t)((image_data_len >> 8) & 0xFF);
            status[3] = (uint8_t)((image_data_len >> 16) & 0xFF);
            status[4] = (uint8_t)((image_data_len >> 24) & 0xFF);
            status[5] = (uint8_t)(image_expected_len & 0xFF);
            status[6] = (uint8_t)((image_expected_len >> 8) & 0xFF);
            status[7] = (uint8_t)((image_expected_len >> 16) & 0xFF);
            status[8] = (uint8_t)((image_expected_len >> 24) & 0xFF);
            status[9] = (uint8_t)(image_frame_id & 0xFF);
            status[10] = (uint8_t)((image_frame_id >> 8) & 0xFF);
            status[11] = (uint8_t)((image_frame_id >> 16) & 0xFF);
            status[12] = (uint8_t)((image_frame_id >> 24) & 0xFF);

            rc = os_mbuf_append(ctxt->om, status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        // Streamed frame write (header + sequential chunks).
        // Supports both X4IM (image) and X4JS (JSON layout)
        {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0) {
                return 0;
            }
            if (len > 600) {
                ESP_LOGW(BLE_TAG, "Write too large for temp buffer: %u", len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            uint8_t tmp[600];
            uint16_t copy_len = 0;
            rc = ble_hs_mbuf_to_flat(ctxt->om, tmp, len, &copy_len);
            if (rc != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }

            uint32_t offset = 0;

            // Check for X4JS (JSON) header first
            if ((json_data_len == 0 || json_data_ready) &&
                copy_len >= X4JS_HDR_LEN &&
                tmp[0] == 'X' && tmp[1] == '4' && tmp[2] == 'J' && tmp[3] == 'S' &&
                tmp[4] == 1) {
                const uint32_t payload_len = read_le_u32(&tmp[8]);
                json_expected_len = payload_len;
                json_data_len = 0;
                json_data_ready = false;

                if (json_file != NULL) {
                    fclose(json_file);
                    json_file = NULL;
                }
                memset(current_json_filename, 0, sizeof(current_json_filename));

                time_t now;
                time(&now);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                strftime(current_json_filename, sizeof(current_json_filename), "/sdcard/layout_%Y%m%d_%H%M%S.json", &timeinfo);

                json_file = fopen(current_json_filename, "wb");
                if (json_file == NULL) {
                    ESP_LOGE(BLE_TAG, "Failed to open JSON file");
                    memset(current_json_filename, 0, sizeof(current_json_filename));
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                }

                offset = X4JS_HDR_LEN;
                ESP_LOGI(BLE_TAG, "JSON start len=%" PRIu32 ", file=%s", json_expected_len, current_json_filename);
                
                if (offset < copy_len) {
                    uint32_t remaining = (uint32_t)(copy_len - offset);
                    uint32_t space = json_expected_len;
                    if (remaining > space) remaining = space;
                    if (remaining > 0) {
                        size_t written = fwrite(&tmp[offset], 1, remaining, json_file);
                        if (written != remaining) {
                            ESP_LOGE(BLE_TAG, "Failed to write JSON initial data (written=%zu, expected=%" PRIu32 ")", written, remaining);
                            fclose(json_file);
                            json_file = NULL;
                            memset(current_json_filename, 0, sizeof(current_json_filename));
                            return BLE_ATT_ERR_INSUFFICIENT_RES;
                        }
                        json_data_len += remaining;
                    }
                }
                
                if (json_data_len >= json_expected_len && json_expected_len > 0) {
                    json_data_ready = true;
                    fclose(json_file);
                    json_file = NULL;
                    ESP_LOGI(BLE_TAG, "JSON complete: %" PRIu32 " bytes", json_data_len);
                }
                return 0;
            }
            
            // Continue JSON chunk
            if (json_file != NULL && !json_data_ready) {
                uint32_t remaining = copy_len;
                uint32_t space = (json_expected_len > json_data_len) ? (json_expected_len - json_data_len) : 0;
                if (remaining > space) remaining = space;
                if (remaining > 0) {
                    size_t written = fwrite(tmp, 1, remaining, json_file);
                    if (written != remaining) {
                        ESP_LOGE(BLE_TAG, "Failed to write JSON data (written=%zu, expected=%" PRIu32 ")", written, remaining);
                        fclose(json_file);
                        json_file = NULL;
                        memset(current_json_filename, 0, sizeof(current_json_filename));
                        return BLE_ATT_ERR_INSUFFICIENT_RES;
                    }
                    json_data_len += remaining;
                }

                if (json_data_len >= json_expected_len && json_expected_len > 0) {
                    json_data_ready = true;
                    fclose(json_file);
                    json_file = NULL;
                    ESP_LOGI(BLE_TAG, "JSON complete: %" PRIu32 " bytes", json_data_len);
                }
                return 0;
            }

            // Check for X4IM (image) header
            if ((image_data_len == 0 || image_data_ready) &&
                copy_len >= X4IM_HDR_LEN &&
                tmp[0] == 'X' && tmp[1] == '4' && tmp[2] == 'I' && tmp[3] == 'M' &&
                tmp[4] == 1 && tmp[5] == 1) {
                const uint32_t payload_len = read_le_u32(&tmp[8]);
                // Accept any size since we use external storage
                image_expected_len = payload_len;
                image_data_len = 0;
                image_data_ready = false;
                image_frame_id++;

                // Close any previous file (e.g., aborted transfer)
                if (image_file != NULL) {
                    fclose(image_file);
                    image_file = NULL;
                }
                memset(current_image_filename, 0, sizeof(current_image_filename));

                // Create filename with timestamp
                time_t now;
                time(&now);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                strftime(current_image_filename, sizeof(current_image_filename), "/sdcard/image_%Y%m%d_%H%M%S.raw", &timeinfo);

                // Open file for writing
                image_file = fopen(current_image_filename, "wb");
                if (image_file == NULL) {
                    ESP_LOGE(BLE_TAG, "Failed to open image file for writing");
                    memset(current_image_filename, 0, sizeof(current_image_filename));
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                }

                offset = X4IM_HDR_LEN;
                ESP_LOGI(BLE_TAG, "frame start id=%" PRIu32 " len=%" PRIu32 ", file=%s", image_frame_id, image_expected_len, current_image_filename);
            }

            // Append remaining payload bytes.
            if (offset < copy_len) {
                uint32_t remaining = (uint32_t)(copy_len - offset);
                uint32_t space = (image_expected_len > image_data_len) ? (image_expected_len - image_data_len) : 0;
                if (remaining > space) {
                    remaining = space;
                }
                if (remaining > 0 && image_file != NULL) {
                    size_t written = fwrite(&tmp[offset], 1, remaining, image_file);
                    if (written != remaining) {
                        ESP_LOGE(BLE_TAG, "Failed to write to image file (written=%zu, expected=%" PRIu32 ")", written, remaining);
                        fclose(image_file);
                        image_file = NULL;
                        memset(current_image_filename, 0, sizeof(current_image_filename));
                        return BLE_ATT_ERR_INSUFFICIENT_RES;
                    }
                    // 修复：使用实际写入的字节数，而不是期望的字节数
                    image_data_len += (uint32_t)written;
                }
            }

            if (!image_data_ready && image_data_len >= image_expected_len && image_expected_len > 0) {
                image_data_ready = true;
                if (image_file != NULL) {
                    fclose(image_file);
                    image_file = NULL;
                }
                ESP_LOGI(BLE_TAG, "Received full frame id=%" PRIu32 " (%" PRIu32 " bytes), saved to %s", image_frame_id, image_data_len, current_image_filename);
            }
            return 0;
        }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// GATT service definition
static const struct ble_gatt_svc_def gatt_svr_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(IMAGE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(IMAGE_DATA_CHAR_UUID),
                .access_cb = image_data_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(CONTROL_CMD_CHAR_UUID),
                .access_cb = control_cmd_chr_access,
                .val_handle = &control_cmd_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, // No more characteristics
            }
        }
    },
    {
        0, // No more services
    }
};



static int gatt_svr_init(void)
{
    int rc = ble_gatts_count_cfg(gatt_svr_defs);
    if (rc != 0) return rc;
    rc = ble_gatts_add_svcs(gatt_svr_defs);
    return rc;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(BLE_TAG, "ADV complete; reason=%d", event->adv_complete.reason);
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(BLE_TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            ble_connected = true;
            ble_advertising = false;
            ble_pending_connection = false;
            {
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    snprintf(ble_peer_addr, sizeof(ble_peer_addr),
                             "%02x:%02x:%02x:%02x:%02x:%02x",
                             desc.peer_id_addr.val[0], desc.peer_id_addr.val[1], desc.peer_id_addr.val[2],
                             desc.peer_id_addr.val[3], desc.peer_id_addr.val[4], desc.peer_id_addr.val[5]);
                }
            }
            ESP_LOGI(BLE_TAG, "BLE server connected, handle=%d", ble_conn_handle);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(BLE_TAG, "Disconnect; reason=%d", event->disconnect.reason);
        ble_connected = false;
        ble_conn_handle = 0;
        memset(ble_peer_addr, 0, sizeof(ble_peer_addr));
        ble_advertising = true;
        cmd_notify_enabled = false;

        // If an image transfer was in progress, close the file and reset state.
        if (image_file != NULL) {
            fclose(image_file);
            image_file = NULL;
        }
        image_data_len = 0;
        image_data_ready = false;
        memset(current_image_filename, 0, sizeof(current_image_filename));
        
        // Also reset JSON state
        if (json_file != NULL) {
            fclose(json_file);
            json_file = NULL;
        }
        json_data_len = 0;
        json_data_ready = false;
        memset(current_json_filename, 0, sizeof(current_json_filename));

        // Restart advertising
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(BLE_TAG, "Subscribe event; attr_handle=%d cur_notify=%d cur_indicate=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify, event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == control_cmd_chr_val_handle) {
            cmd_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(BLE_TAG, "CMD notify %s",
                     cmd_notify_enabled ? "ENABLED" : "DISABLED");
        }
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(BLE_TAG, "Passkey action event; action=%d",
                 event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            // Display passkey (for this demo, we'll use a fixed passkey)
            struct ble_sm_io pk;
            pk.action = event->passkey.params.action;
            pk.passkey = 123456; // Fixed passkey for demo
            ESP_LOGI(BLE_TAG, "Display passkey: %06d", pk.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(BLE_TAG, "Encryption change event; status=%d",
                 event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(BLE_TAG, "Discovery complete; reason=%d", event->disc_complete.reason);
        return 0;
    default:
        return 0;
    }
}

static int start_advertising(void)
{
    int rc;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return rc;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Connectable undirected advertising
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_gap_adv_start failed: %d", rc);
        return rc;
    }

    ble_advertising = true;

    ESP_LOGI(BLE_TAG, "Advertising started (connectable), name=%s", DEVICE_NAME);
    return 0;
}


static void ble_on_sync(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return;
    }

    ESP_LOGI(BLE_TAG, "BLE synced; name=%s", DEVICE_NAME);
    (void)start_advertising();
}

void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void bt_init(void)
{
    ESP_LOGI(BLE_TAG, "Starting BLE initialization...");

    // Recommended NimBLE sequence (ESP-IDF): nimble_port_init handles controller + transport
    ESP_ERROR_CHECK(nimble_port_init());

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Configure security manager for pairing
    ble_hs_cfg.sm_bonding = 1;  // Enable bonding
    ble_hs_cfg.sm_mitm = 1;     // Enable MITM protection
    ble_hs_cfg.sm_sc = 1;       // Enable Secure Connections
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;  // Display passkey only
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    (void)ble_svc_gap_device_name_set(DEVICE_NAME);
    (void)gatt_svr_init();
    nimble_port_freertos_init(host_task);

    ESP_LOGI(BLE_TAG, "BLE initialized in SERVER mode with image service");

    // Read and log local BLE (BT) MAC address
    {
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
            sprintf(ble_local_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            ESP_LOGI(BLE_TAG, "Local BLE MAC: %s", ble_local_addr);
        } else {
            strcpy(ble_local_addr, "00:00:00:00:00:00");
            ESP_LOGW(BLE_TAG, "Failed to read local BLE MAC");
        }
    }
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
// 不要 ESP_ERROR_CHECK 直接 abort，改为返回错误让系统继续运行（至少保证 EPD/LVGL 可用）。
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

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
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
    ESP_LOGI("MAIN", "Waiting 1 second before initializing BLE/WiFi to prevent brownout...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Initialize BLE AFTER EPD (ESP32-C3 requires BLE controller init before Wi-Fi)
    ESP_LOGI("MAIN", "Initializing BLE...");
    bt_init();

    // WiFi initialization - re-enabled for web interface
    ESP_LOGI("MAIN", "Initializing WiFi...");
    esp_err_t wifi_ret = wifi_init_sta();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW("MAIN", "WiFi init failed (%s); continuing without WiFi", esp_err_to_name(wifi_ret));
    }

    // Initialize SD card
    ESP_LOGI("MAIN", "Initializing SD card...");
    esp_err_t sd_ret = sd_card_init();
    if (sd_ret != ESP_OK) {
        ESP_LOGW("MAIN", "SD card initialization failed, but system will continue");
        ESP_LOGW("MAIN", "File browser and SD-related features will be unavailable");
    }

    // ============================================================================
    // LVGL GUI 初始化
    // ============================================================================
    ESP_LOGI("MAIN", "Initializing LVGL GUI system...");

    // 1. 初始化 LVGL 显示驱动（framebuffer 在 lvgl_driver.c 中静态分配）
    lvgl_display_init();

    // 2. 初始化 LVGL 输入设备（按键）
    lv_indev_t *indev = lvgl_input_init();
    (void)indev;

    // 3. 创建 LVGL tick 任务（10ms tick）
    // 降低优先级到 3，避免阻塞 IDLE 任务
    xTaskCreate(lvgl_tick_task, "lvgl_tick", 2048, NULL, 3, NULL);

    // 4. 创建欢迎屏幕（替代 display_welcome_screen）
    ESP_LOGI("LVGL", "Creating welcome screen with system info...");
    lvgl_demo_create_welcome_screen(
        read_battery_voltage_mv(),
        read_battery_percentage(),
        is_charging(),
        VERSION_FULL,
        indev  // 传入输入设备，用于设置焦点
    );

    // 5. 在启动 LVGL timer 任务之前，先在当前线程渲染一次。
    // 重要：LVGL 不是线程安全的，不能同时在多个任务里调用 lv_timer_handler/lv_task_handler。
    ESP_LOGI("LVGL", "Rendering UI (single-threaded) before starting LVGL timer task...");
    for (int i = 0; i < 6; i++) {
        lv_timer_handler();
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }

    // 6. 刷新EPD显示（异步，非阻塞）
    ESP_LOGI("LVGL", "Refreshing EPD with welcome screen (async)...");
    lvgl_display_refresh();

    // 7. 等待EPD刷新完成（异步刷新在后台进行）
    ESP_LOGI("LVGL", "EPD refresh started in background, continuing initialization...");
    vTaskDelay(500 / portTICK_PERIOD_MS);  // 短暂等待确保刷新任务已启动

    // 8. 创建 LVGL 定时器任务（处理 UI 更新）
    // 放到初次渲染/EPD 刷新之后，避免与上面的 lv_timer_handler 并发。
    xTaskCreate(lvgl_timer_task, "lvgl_timer", 4096, NULL, 2, NULL);

    ESP_LOGI("MAIN", "LVGL GUI initialized successfully!");
    ESP_LOGI("MAIN", "Use UP/DOWN buttons to navigate, CONFIRM to select");

    ESP_LOGI("MAIN", "System initialized. LVGL is handling UI events.");
    ESP_LOGI("MAIN", "Main task ending, FreeRTOS tasks continue running...");
}

