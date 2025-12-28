#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "esp_littlefs.h"
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
#include "esp_http_server.h"
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
#include "GUI_Paint.h"
#include "ImageData.h"

// BLE connection state management
static bool ble_connected = false;
static bool ble_advertising = false;
static uint16_t ble_conn_handle = 0;
static char ble_peer_addr[18] = {0};
static bool ble_pending_connection = false;
static char ble_local_addr[18] = {0};
static uint8_t own_addr_type;

// Define button pin for sleep (using GPIO 9, which is available and not used by EPD)
#define SLEEP_BUTTON_PIN GPIO_NUM_9

// Define driver selection
#define TEST_DRIVER_SSD1677 1
#define TEST_DRIVER_GDEQ0426T82 2
#define TEST_DRIVER_SSD1681 3

#define CURRENT_DRIVER TEST_DRIVER_SSD1681  // Change this to test different drivers

// Forward declarations
httpd_handle_t start_webserver(void);
static int start_advertising(void);
esp_err_t sd_card_init(void);
esp_err_t save_image_to_sd(const uint8_t *image_data, size_t data_len);
void sd_card_test_read_write(const char *mount_point);

#define STORAGE_MOUNT_POINT "/littlefs"
#define SPI_DMA_CHAN    1

// SD card pins (adjust according to your hardware setup for ESP32-C3)
#define PIN_NUM_MISO  GPIO_NUM_6
#define PIN_NUM_MOSI  GPIO_NUM_7
#define PIN_NUM_CLK   GPIO_NUM_8
#define PIN_NUM_CS    GPIO_NUM_9

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
                        fwrite(&tmp[offset], 1, remaining, json_file);
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
                    fwrite(tmp, 1, remaining, json_file);
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
                        ESP_LOGE(BLE_TAG, "Failed to write to image file");
                        fclose(image_file);
                        image_file = NULL;
                        memset(current_image_filename, 0, sizeof(current_image_filename));
                        return BLE_ATT_ERR_INSUFFICIENT_RES;
                    }
                    image_data_len += remaining;
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

static int security_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(BLE_TAG, "Security: Passkey action event");
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
        ESP_LOGI(BLE_TAG, "Security: Encryption change event; status=%d",
                 event->enc_change.status);
        return 0;
    default:
        return 0;
    }
}

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

        // Start HTTP server after WiFi is connected
        start_webserver();
    }
}

// WiFi initialization function
void wifi_init_sta(void)
{
    ESP_LOGI("WIFI", "Initializing WiFi in station mode...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Simple WiFi config — updated with requested credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "foxwifi-plus",
            .password = "epdc1984",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "WiFi initialization completed. Connecting...");
}

// SD card initialization function
esp_err_t sd_card_init(void)
{
    ESP_LOGI("SD", "Initializing SD card");

    esp_err_t ret;

    // First, try mounting LittleFS at STORAGE_MOUNT_POINT
    ESP_LOGI("STORAGE", "Attempting to mount LittleFS at %s", STORAGE_MOUNT_POINT);
    
    esp_vfs_littlefs_conf_t littlefs_conf = {
        .base_path = STORAGE_MOUNT_POINT,
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    ret = esp_vfs_littlefs_register(&littlefs_conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        ret = esp_littlefs_info(littlefs_conf.partition_label, &total, &used);
        if (ret == ESP_OK) {
            ESP_LOGI("STORAGE", "LittleFS mounted successfully");
            ESP_LOGI("STORAGE", "Partition size: total: %d, used: %d", total, used);
        }
        // Run basic tests on the mounted FS
        sd_card_test_read_write(STORAGE_MOUNT_POINT);
        return ESP_OK;
    }

    ESP_LOGW("STORAGE", "LittleFS mount failed (%s), falling back to SD card", esp_err_to_name(ret));

    // Options for mounting the FAT filesystem on SD card.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char sd_mount_point[] = "/sdcard";

    ESP_LOGI("SD", "Initializing SD card using SPI peripheral");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 1000;  // Lower frequency for better compatibility

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // Initialize SPI bus - GPIO 6/7/8/9 are not all IOMUX pins, so use GPIO matrix
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE("SD", "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI("SD", "SPI bus initialized successfully");

    ESP_LOGI("SD", "Mounting FAT filesystem at %s", sd_mount_point);
    ret = esp_vfs_fat_sdspi_mount(sd_mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SD", "Failed to mount filesystem. Set format_if_mount_failed = true to format.");
        } else {
            ESP_LOGE("SD", "Failed to initialize the card (%s). Ensure SD card lines have pull-ups.", esp_err_to_name(ret));
        }
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


void test_driver(int driver) {
    printf("Testing driver: %d\n", driver);
    switch (driver) {
        case TEST_DRIVER_SSD1677:
            printf("Using SSD1677 (EPD_4in26)\n");
            EPD_4in26_Init();
            printf("EPD_4in26_Init done\n");
            EPD_4in26_Clear();
            printf("EPD_4in26_Clear done\n");
            // Create and display black image
            UBYTE *Image;
            UWORD Imagesize = ((EPD_4in26_WIDTH % 8 == 0) ? (EPD_4in26_WIDTH / 8) : (EPD_4in26_WIDTH / 8 + 1)) * EPD_4in26_HEIGHT;
            if ((Image = (UBYTE *)malloc(Imagesize)) == NULL) {
                printf("Failed to apply for memory...\n");
                return;
            }
            memset(Image, 0x00, Imagesize);  // Black
            EPD_4in26_Display(Image);
            printf("EPD_4in26_Display (black) done\n");
            free(Image);
            EPD_4in26_Sleep();
            printf("EPD_4in26_Sleep done\n");
            break;
        case TEST_DRIVER_GDEQ0426T82:
            printf("Using GDEQ0426T82 (placeholder - need to implement)\n");
            // Placeholder: implement GDEQ0426T82 init and display
            break;
        case TEST_DRIVER_SSD1681:
            printf("Using SSD1681 (placeholder - implementing basic sequence)\n");
            // Basic SSD1681 initialization sequence (simplified)
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x12); // SWRESET
            vTaskDelay(10 / portTICK_PERIOD_MS);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x01); // Driver output control
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7); // Height low
            DEV_SPI_WriteByte(0x00); // Height high
            DEV_SPI_WriteByte(0x01); // Other
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x11); // Data entry mode
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x01); // Y increment, X increment
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x44); // Set RAM X address
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x18);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x45); // Set RAM Y address
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7);
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x00);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x3C); // Border waveform
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x05);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x18); // Temperature sensor
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x80);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x4E); // Set RAM X counter
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x00);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x4F); // Set RAM Y counter
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7);
            DEV_SPI_WriteByte(0x00);
            
            printf("SSD1681 init done\n");
            
            // Clear screen (send white data)
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x24); // Write RAM
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            for (int i = 0; i < 5000; i++) { // Approximate size
                DEV_SPI_WriteByte(0xFF); // White
            }
            
            printf("SSD1681 clear done\n");
            
            // Display update
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x22); // Display update control
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xF7);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x20); // Master activation
            // Wait for BUSY
            while (DEV_Digital_Read(EPD_BUSY_PIN) == 1) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            
            printf("SSD1681 display done\n");
            
            // Sleep
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x10); // Deep sleep
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x01);
            
            printf("SSD1681 sleep done\n");
            break;
        default:
            printf("Unknown driver\n");
            break;
    }
}

// Button press detection and deep sleep function
void check_button_and_sleep(void) {
    // Configure button pin as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SLEEP_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    printf("Press button on GPIO %d to enter deep sleep...\n", SLEEP_BUTTON_PIN);
    // EPD re-test commented out - only BLE and WiFi
    // printf("Double-click button on GPIO %d to re-test EPD display...\n", SLEEP_BUTTON_PIN);

    int last_button_state = 1;  // Button not pressed (high with pull-up)

    while (1) {
        int button_state = gpio_get_level(SLEEP_BUTTON_PIN);

        // Detect button press (falling edge)
        if (last_button_state == 1 && button_state == 0) {
            // Debounce delay
            vTaskDelay(50 / portTICK_PERIOD_MS);

            // Wait for button release
            while (gpio_get_level(SLEEP_BUTTON_PIN) == 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }

            // Single click - enter deep sleep
            printf("Button pressed! Entering deep sleep in 1 second...\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Final confirmation delay

            printf("Entering deep sleep now. Press reset button to wake up.\n");

            // For ESP32-C3, ext0 wakeup may not be available, so we'll just enter deep sleep
            // User needs to press reset button to wake up
            esp_deep_sleep_start();
        }

        last_button_state = button_state;
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Poll every 10ms for better responsiveness
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

    // EPD initialization commented out - only BLE and WiFi
    /*
    // Initialize SPI and e-Paper FIRST (before BLE/WiFi to avoid power issues)
    printf("Initializing DEV_Module...\n");
    DEV_Module_Init();
    printf("DEV_Module_Init done\n");

    // Test SPI communication
    printf("Testing SPI communication...\n");
    DEV_SPI_WriteByte(0x00);  // Send a test byte
    printf("SPI test done\n");

    // Low-level hardware self-check: pulse RST, toggle CS, send SPI pattern, read BUSY
    printf("Starting low-level hardware self-check...\n");
    // Pulse RST
    DEV_Digital_Write(EPD_RST_PIN, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Toggle CS and send pattern
    for (int pass = 0; pass < 3; pass++) {
        printf("Self-check pass %d: CS LOW, send 0xAA,0x55 then CS HIGH\n", pass+1);
        DEV_Digital_Write(EPD_CS_PIN, 0);
        DEV_SPI_WriteByte(0xAA);
        DEV_SPI_WriteByte(0x55);
        DEV_Digital_Write(EPD_CS_PIN, 1);
        // Read BUSY 10 times
        for (int i = 0; i < 10; i++) {
            int busy = DEV_Digital_Read(EPD_BUSY_PIN);
            printf("BUSY read %d: %d\n", i, busy);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
    printf("Low-level self-check finished.\n");
    */

    // Add delay before initializing high-power components to prevent brownout
    ESP_LOGI("MAIN", "Waiting 2 seconds before initializing BLE/WiFi to prevent brownout...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // Initialize BLE AFTER EPD (ESP32-C3 requires BLE controller init before Wi-Fi)
    ESP_LOGI("MAIN", "Initializing BLE...");
    bt_init();

    // WiFi initialization - re-enabled for web interface
    ESP_LOGI("MAIN", "Initializing WiFi...");
    wifi_init_sta();

    // Initialize SD card
    ESP_LOGI("MAIN", "Initializing SD card...");
    sd_card_init();

    // HTTP server will be started automatically after WiFi connects
    // ESP_LOGI("MAIN", "Starting HTTP server...");
    // start_webserver();

    // EPD test commented out - only BLE and WiFi
    /*
    // Test different drivers
    test_driver(CURRENT_DRIVER);

    printf("Test completed!\n");
    */

    // Start button monitoring for deep sleep
    // check_button_and_sleep();  // Commented out to allow normal operation
}

// BLE related functions
static void bleprph_on_reset(int reason)
{
    ESP_LOGI("BLE", "Resetting state; reason=%d\n", reason);
}

// HTTP server functions
// Large HTML content moved to global constant to avoid stack overflow
static const char* html_template =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32 EPD Control</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>"
    "body { font-family: Arial, sans-serif; text-align: center; margin: 20px; }"
    ".status { background-color: #f0f0f0; padding: 20px; border-radius: 10px; margin: 20px auto; max-width: 800px; }"
    ".btns { margin: 12px 0; }"
    ".btns button { padding: 10px 16px; margin: 0 6px; font-size: 16px; }"
    "#jsonDisplay { border: 1px solid #ddd; max-width: 100%%; height: 600px; overflow: auto; background: #fff; text-align: left; padding: 10px; white-space: pre-wrap; font-family: monospace; font-size: 12px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>ESP32 EPD Control System</h1>"
    "<div class=\"status\">"
    "<h2>System Status</h2>"
    "<p id=\"bleStatus\">BLE: Checking...</p>"
    "<p>BLE MAC: %s</p>"
    "<p>WiFi: Connected</p>"
    "<div class=\"btns\">"
    "<button onclick=\"sendCmd('prev')\">Previous</button>"
    "<button onclick=\"sendCmd('next')\">Next</button>"
    "<button onclick=\"sendCmd('capture')\">Refresh</button>"
    "</div>"
    "<p id=\"imgStatus\">Layout: (unknown)</p>"
    "<div id=\"jsonDisplay\">Waiting for layout data...</div>"
    "</div>"
    "<script>"
    "const statusEl=document.getElementById('imgStatus');"
    "const bleStatusEl=document.getElementById('bleStatus');"
    "const jsonDisplayEl=document.getElementById('jsonDisplay');"
    "async function sendCmd(cmd){"
    "  try{ await fetch('/cmd?cmd='+encodeURIComponent(cmd)); }catch(e){}"
    "  pollAndRender();"
    "}"
    "async function checkBleStatus(){"
    "  try{"
    "    const st=await (await fetch('/cmd?cmd=ble_status')).json();"
    "    let statusText='BLE: ';"
    "    if(st.ble_connected){"
    "      statusText+='Connected to ' + st.peer_addr;"
    "    }else if(st.ble_advertising){"
    "      statusText+='Advertising...';"
    "    }else{"
    "      statusText+='Disconnected';"
    "    }"
    "    bleStatusEl.textContent=statusText;"
    "  }catch(e){ bleStatusEl.textContent='BLE: Error checking status'; }"
    "}"
    "async function renderJson(){"
    "  try{"
    "    const json=await (await fetch('/cmd?cmd=get_layout')).text();"
    "    if(json && json.length > 0){"
    "      jsonDisplayEl.textContent=json;"
    "      statusEl.textContent='Layout: Loaded ('+json.length+' bytes)';"
    "    }else{"
    "      jsonDisplayEl.textContent='No layout data';"
    "      statusEl.textContent='Layout: No data';"
    "    }"
    "  }catch(e){"
    "    jsonDisplayEl.textContent='Error loading layout: '+e;"
    "    statusEl.textContent='Layout: Error';"
    "  }"
    "}"
    "async function pollAndRender(){"
    "  await renderJson();"
    "  checkBleStatus();"
    "}"
    "setInterval(pollAndRender, 2000);"
    "pollAndRender();"
    "</script>"
    "</body>"
    "</html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *resp = malloc(4096);  // Use heap allocation instead of stack
    if (!resp) {
        ESP_LOGE("HTTP", "Failed to allocate memory for HTML response");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int len = snprintf(resp, 4096, html_template, ble_local_addr);

    if (len >= 4096) {
        ESP_LOGE("HTTP", "HTML response truncated, needed %d bytes", len);
    }

    httpd_resp_send(req, resp, strlen(resp));
    free(resp);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
};

// Command handler for BLE control
static esp_err_t cmd_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;
    char response[256];

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI("HTTP", "Found URL query => %s", buf);
            char param[32];

            // Handle ble_status command
            if (httpd_query_key_value(buf, "cmd", param, sizeof(param)) == ESP_OK) {
                if (strcmp(param, "ble_status") == 0) {
                    sprintf(response, "{\"ble_connected\":%s,\"ble_advertising\":%s,\"ble_pending\":%s,\"peer_addr\":\"%s\",\"local_addr\":\"%s\",\"cmd_notify\":%s}",
                        ble_connected ? "true" : "false",
                        ble_advertising ? "true" : "false",
                        ble_pending_connection ? "true" : "false",
                        ble_peer_addr,
                        ble_local_addr,
                        cmd_notify_enabled ? "true" : "false");
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, response, strlen(response));
                    free(buf);
                    return ESP_OK;
                }
                else if (strcmp(param, "ble_accept") == 0) {
                    if (ble_pending_connection) {
                        // For NimBLE, we just set the connection as accepted
                        // The actual connection handling is done in GAP events
                        ble_pending_connection = false;
                        ble_connected = true;
                        sprintf(response, "{\"status\":\"accepted\",\"message\":\"BLE connection accepted\"}");
                    } else {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"No pending BLE connection\"}");
                    }
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, response, strlen(response));
                    free(buf);
                    return ESP_OK;
                }
                else if (strcmp(param, "ble_reject") == 0) {
                    if (ble_pending_connection) {
                        // For NimBLE, disconnect if we're rejecting
                        if (ble_conn_handle != 0) {
                            ble_gap_terminate(ble_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                        }
                        ble_pending_connection = false;
                        sprintf(response, "{\"status\":\"rejected\",\"message\":\"BLE connection rejected\"}");
                    } else {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"No pending BLE connection\"}");
                    }
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, response, strlen(response));
                    free(buf);
                    return ESP_OK;
                }
                // Handle get_image command to retrieve image data from BLE
                else if (strcmp(param, "get_image") == 0) {
                    if (image_data_ready && image_data_len > 0 && strlen(current_image_filename) > 0) {
                        FILE *f = fopen(current_image_filename, "rb");
                        if (f != NULL) {
                            httpd_resp_set_type(req, "application/octet-stream");
                            uint8_t buffer[1024];
                            size_t read_bytes;
                            while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                                httpd_resp_send_chunk(req, (const char*)buffer, read_bytes);
                            }
                            httpd_resp_send_chunk(req, NULL, 0); // End chunked response
                            fclose(f);
                            ESP_LOGI("HTTP", "Sent image data: %" PRIu32 " bytes from %s", image_data_len, current_image_filename);
                        } else {
                            sprintf(response, "{\"status\":\"error\",\"message\":\"Failed to open image file\"}");
                            httpd_resp_set_type(req, "application/json");
                            httpd_resp_send(req, response, strlen(response));
                        }
                    } else {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"No image data available\"}");
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, response, strlen(response));
                    }
                    free(buf);
                    return ESP_OK;
                }
                // Handle get_layout command to retrieve JSON layout data
                else if (strcmp(param, "get_layout") == 0) {
                    if (json_data_ready && json_data_len > 0 && strlen(current_json_filename) > 0) {
                        FILE *f = fopen(current_json_filename, "rb");
                        if (f != NULL) {
                            httpd_resp_set_type(req, "application/json");
                            uint8_t buffer[1024];
                            size_t read_bytes;
                            while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                                httpd_resp_send_chunk(req, (const char*)buffer, read_bytes);
                            }
                            httpd_resp_send_chunk(req, NULL, 0);
                            fclose(f);
                            ESP_LOGI("HTTP", "Sent JSON layout: %" PRIu32 " bytes from %s", json_data_len, current_json_filename);
                        } else {
                            sprintf(response, "{\"status\":\"error\",\"message\":\"Failed to open JSON file\"}");
                            httpd_resp_set_type(req, "application/json");
                            httpd_resp_send(req, response, strlen(response));
                        }
                    } else {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"No JSON data available\"}");
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, response, strlen(response));
                    }
                    free(buf);
                    return ESP_OK;
                }
                // Handle image_status command
                else if (strcmp(param, "image_status") == 0) {
                    sprintf(response, "{\"image_ready\":%s,\"image_size\":%" PRIu32 ",\"expected_size\":%" PRIu32 ",\"file\":\"%s\"}",
                        image_data_ready ? "true" : "false",
                        image_data_len,
                        image_expected_len,
                        current_image_filename);
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, response, strlen(response));
                    free(buf);
                    return ESP_OK;
                }
                // Page control commands: prev / next / capture
                else if (strcmp(param, "prev") == 0 || strcmp(param, "next") == 0 || strcmp(param, "capture") == 0) {
                    strncpy(last_control_cmd, param, sizeof(last_control_cmd) - 1);
                    last_control_cmd[sizeof(last_control_cmd) - 1] = 0;

                    if (!ble_connected || ble_conn_handle == 0 || control_cmd_chr_val_handle == 0) {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"BLE not connected\"}");
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, response, strlen(response));
                        free(buf);
                        return ESP_OK;
                    }

                    if (!cmd_notify_enabled) {
                        ESP_LOGW("HTTP", "CMD notify not enabled; skipping notify for cmd=%s", last_control_cmd);
                        sprintf(response, "{\"status\":\"error\",\"message\":\"notify not enabled\"}");
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, response, strlen(response));
                        free(buf);
                        return ESP_OK;
                    }

                    struct os_mbuf *om = ble_hs_mbuf_from_flat(last_control_cmd, strlen(last_control_cmd));
                    if (!om) {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"mbuf alloc failed\"}");
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, response, strlen(response));
                        free(buf);
                        return ESP_OK;
                    }

                    int rc = ble_gatts_notify_custom(ble_conn_handle, control_cmd_chr_val_handle, om);
                    if (rc != 0) {
                        sprintf(response, "{\"status\":\"error\",\"message\":\"notify failed\",\"rc\":%d}", rc);
                    } else {
                        sprintf(response, "{\"status\":\"ok\",\"cmd\":\"%s\"}", last_control_cmd);
                    }
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, response, strlen(response));
                    free(buf);
                    return ESP_OK;
                }
            }
        }
        free(buf);
    }

    // Default response
    sprintf(response, "{\"status\":\"error\",\"message\":\"Invalid command\"}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}



static const httpd_uri_t cmd = {
    .uri       = "/cmd",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI("HTTP", "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &cmd);
        ESP_LOGI("HTTP", "Web server started successfully");
        return server;
    }

    ESP_LOGE("HTTP", "Failed to start web server");
    return NULL;
}

// Function to save image data to SD card
esp_err_t save_image_to_sd(const uint8_t *image_data, size_t data_len) {
    if (image_data == NULL || data_len == 0) {
        ESP_LOGE("SD", "Invalid image data");
        return ESP_ERR_INVALID_ARG;
    }

    // Create filename with timestamp
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[64];
    strftime(filename, sizeof(filename), "/sdcard/image_%Y%m%d_%H%M%S.raw", &timeinfo);

    ESP_LOGI("SD", "Saving image to %s", filename);

    // Open file for writing
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        ESP_LOGE("SD", "Failed to open file for writing");
        return ESP_ERR_NOT_FOUND;
    }

    // Write image data
    size_t written = fwrite(image_data, 1, data_len, f);
    fclose(f);

    if (written != data_len) {
        ESP_LOGE("SD", "Failed to write complete image data. Written: %d, Expected: %d", written, data_len);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI("SD", "Image saved successfully (%d bytes)", written);
    return ESP_OK;
}


