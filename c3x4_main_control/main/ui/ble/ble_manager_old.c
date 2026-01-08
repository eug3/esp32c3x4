/**
 * @file ble_manager.c
 * @brief 蓝牙管理器实现 - 处理蓝牙通信的核心功能
 *
 * 使用 NimBLE 栈实现蓝牙扫描、连接和数据传输功能
 */

#include "ble_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "esp_nimble_hci.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#ifndef BLE_OWN_ADDR_PUBLIC
#define BLE_OWN_ADDR_PUBLIC 0
#endif
#include "os/os_mbuf.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

// Forward declaration for ble_store_config_init
extern void ble_store_config_init(void);

static const char *TAG = "BLE_MANAGER";

// 蓝牙管理器状态
static struct {
    bool initialized;
    bool scanning;
    bool advertising;
    bool connected;
    uint8_t connected_addr[6];
    uint16_t connected_handle;

    // GATT server ready flag
    bool gatt_ready;
    
    // 回调函数
    ble_on_device_found_cb device_found_cb;
    ble_on_connect_cb connect_cb;
    ble_on_data_received_cb data_received_cb;
} s_ble_state = {
    .initialized = false,
    .scanning = false,
    .advertising = false,
    .connected = false,
    .device_found_cb = NULL,
    .connect_cb = NULL,
    .data_received_cb = NULL,
    .gatt_ready = false,
};

static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static void reset_gatt_state(void)
{
    s_ble_state.gatt_ready = false;
}

static void parse_adv_for_uuid128(const uint8_t *data, uint8_t data_len, ble_device_info_t *out)
{
    if (!out) return;
    out->has_service_uuid128 = false;
    memset(out->service_uuid128_le, 0, sizeof(out->service_uuid128_le));
    if (!data || data_len == 0) return;

    uint8_t i = 0;
    while (i < data_len) {
        uint8_t len = data[i];
        if (len == 0) break;
        if (i + len + 1 > data_len) break;
        uint8_t type = data[i + 1];
        const uint8_t *payload = &data[i + 2];
        uint8_t payload_len = len - 1;

        // 0x06: Incomplete List of 128-bit Service Class UUIDs
        // 0x07: Complete List of 128-bit Service Class UUIDs
        if ((type == 0x06 || type == 0x07) && payload_len >= 16) {
            memcpy(out->service_uuid128_le, payload, 16);
            out->has_service_uuid128 = true;
            return;
        }

        i += len + 1;
    }
}

static int start_advertising(void);
static void ble_on_sync(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief GAP扫描事件处理
 */
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            // 发现新设备
            if (s_ble_state.device_found_cb != NULL) {
                ble_device_info_t device_info = {0};
                memcpy(device_info.addr, event->disc.addr.val, 6);
                device_info.rssi = event->disc.rssi;
                
                // 尝试获取设备名称
                uint8_t *data = (uint8_t *)event->disc.data;
                uint8_t data_len = event->disc.length_data;
                uint8_t i = 0;
                
                while (i < data_len) {
                    uint8_t len = data[i];
                    if (i + len + 1 > data_len) break;
                    
                    uint8_t type = data[i + 1];
                    
                    // AD Type: 0x09 = Complete Local Name, 0x08 = Shortened Local Name
                    if ((type == 0x09 || type == 0x08) && len > 1) {
                        uint8_t name_len = len - 1;
                        if (name_len > sizeof(device_info.name) - 1) {
                            name_len = sizeof(device_info.name) - 1;
                        }
                        memcpy(device_info.name, &data[i + 2], name_len);
                        device_info.name[name_len] = '\0';
                        device_info.name_len = name_len;
                        break;
                    }
                    
                    i += len + 1;
                }
                
                s_ble_state.device_found_cb(&device_info);
            }
            break;
            
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                ESP_LOGW(TAG, "Connect failed, status=%d", event->connect.status);
                s_ble_state.connected = false;
                s_ble_state.connected_handle = 0;
                memset(s_ble_state.connected_addr, 0, sizeof(s_ble_state.connected_addr));
                reset_gatt_state();
                start_advertising();
                break;
            }

            ESP_LOGI(TAG, "Device connected");
            s_ble_state.connected = true;
            s_ble_state.connected_handle = event->connect.conn_handle;
            s_ble_state.advertising = false;
            {
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    memcpy(s_ble_state.connected_addr, desc.peer_id_addr.val, 6);
                } else {
                    memset(s_ble_state.connected_addr, 0, sizeof(s_ble_state.connected_addr));
                }
            }
            reset_gatt_state();
            s_ble_state.gatt_ready = true;
            
            if (s_ble_state.connect_cb != NULL) {
                s_ble_state.connect_cb(true);
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            // 连接断开
            ESP_LOGI(TAG, "Device disconnected");
            s_ble_state.connected = false;
            s_ble_state.connected_handle = 0;
            memset(s_ble_state.connected_addr, 0, 6);
            reset_gatt_state();
            start_advertising();
            
            if (s_ble_state.connect_cb != NULL) {
                s_ble_state.connect_cb(false);
            }
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_ble_state.advertising = false;
            start_advertising();
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            // Receive notification/indication from peer
            if (s_ble_state.data_received_cb != NULL && event->notify_rx.om != NULL) {
                uint16_t om_len = OS_MBUF_PKTLEN(event->notify_rx.om);
                if (om_len > 0) {
                    uint8_t *buf = (uint8_t *)malloc(om_len);
                    if (buf != NULL) {
                        int copied = os_mbuf_copydata(event->notify_rx.om, 0, om_len, buf);
                        if (copied == 0) {
                            s_ble_state.data_received_cb(buf, om_len);
                        }
                        free(buf);
                    }
                }
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

/**
 * @brief BLE栈事件处理
 */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    
    // 等待BLE栈初始化完成
    nimble_port_run();
    
    nimble_port_freertos_deinit();
}

static int start_advertising(void)
{
    ESP_LOGI(TAG, "=== Heap before advertising ===");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Largest free block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"MFP-EPD";
    fields.name_len = strlen("MFP-EPD");
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields rc=%d", rc);
        return rc;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    ESP_LOGI(TAG, "DEBUG: adv_params configured - conn_mode=%d, disc_mode=%d, itvl_min=%d, itvl_max=%d, channel_map=0x%02x",
             adv_params.conn_mode, adv_params.disc_mode, adv_params.itvl_min, adv_params.itvl_max, adv_params.channel_map);

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising rc=%d", rc);
        return rc;
    }

    s_ble_state.advertising = true;
    ESP_LOGI(TAG, "Advertising started (name=%s)", "MFP-EPD");
    return 0;
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "ble_on_sync() called");
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Calling start_advertising from ble_on_sync");
    start_advertising();
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool ble_manager_init(void)
{
    if (s_ble_state.initialized) {
        ESP_LOGW(TAG, "BLE manager already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing BLE manager...");
    
    // 打印堆内存状态 - 初始化前
    ESP_LOGI(TAG, "=== Heap before BLE init ===");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "  Largest free block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    // 初始化NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS flash");
        return false;
    }
    
    // 释放经典蓝牙控制器内存
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release classic BT memory");
        return false;
    }
    
    // 释放WiFi内存（如果未使用）
    ret = esp_wifi_deinit();
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGI(TAG, "WiFi not initialized, releasing WiFi memory");
        // WiFi未初始化，直接释放内存
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi deinit returned: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Initializing NimBLE host (will init controller internally)...");
    
    // 初始化 NimBLE host（内部会初始化控制器与HCI传输）
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "This usually indicates insufficient memory or duplicate initialization");
        ESP_LOGE(TAG, "  Free heap now: %lu bytes", esp_get_free_heap_size());
        ESP_LOGE(TAG, "  Largest free block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return false;
    }
    
    ESP_LOGI(TAG, "=== Heap after NimBLE host init ===");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Largest free block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    ESP_LOGI(TAG, "NimBLE host initialized, configuring GAP/GATT services...");
    
    ESP_LOGI(TAG, "=== Heap after NimBLE init ===");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "  Largest free block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    // 配置BLE GAP
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    // 设置本地设备名称
    ble_svc_gap_device_name_set("MFP-EPD");

    // 配置 BLE 存储（安全材料的读写）
    // 这对于配对和密钥管理是必需的，即使我们禁用了安全功能
    ble_store_config_init();

    // GAP/GATT ready -> start advertising after host sync
    ble_hs_cfg.sync_cb = ble_on_sync;
    
    // GAP 事件通过各 API 传入 handler（无需全局设置）
    
    // 启动NimBLE主机任务
    nimble_port_freertos_init(ble_host_task);
    
    s_ble_state.initialized = true;
    ESP_LOGI(TAG, "BLE manager initialized successfully");
    
    return true;
}

void ble_manager_deinit(void)
{
    if (!s_ble_state.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing BLE manager...");

    if (s_ble_state.advertising) {
        ble_manager_stop_advertising();
    }
    
    if (s_ble_state.scanning) {
        ble_manager_stop_scan();
    }
    
    if (s_ble_state.connected) {
        ble_manager_disconnect();
    }
    
    // NimBLE 清理 - 按初始化的逆序（nimble_port_deinit 会处理控制器关闭）
    nimble_port_deinit();
    
    s_ble_state.initialized = false;
    ESP_LOGI(TAG, "BLE manager deinitialized");
}

void ble_manager_register_device_found_cb(ble_on_device_found_cb cb)
{
    s_ble_state.device_found_cb = cb;
}

void ble_manager_register_connect_cb(ble_on_connect_cb cb)
{
    s_ble_state.connect_cb = cb;
}

void ble_manager_register_data_received_cb(ble_on_data_received_cb cb)
{
    s_ble_state.data_received_cb = cb;
}

bool ble_manager_start_advertising(void)
{
    if (!s_ble_state.initialized) {
        ESP_LOGE(TAG, "BLE manager not initialized");
        return false;
    }

    int rc = start_advertising();
    return rc == 0;
}

bool ble_manager_stop_advertising(void)
{
    if (!s_ble_state.initialized || !s_ble_state.advertising) {
        return true;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop advertising rc=%d", rc);
        return false;
    }

    s_ble_state.advertising = false;
    ESP_LOGI(TAG, "Advertising stopped");
    return true;
}

bool ble_manager_start_scan(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "Scan disabled: device runs as peripheral and only advertises");
    return false;
}

bool ble_manager_stop_scan(void)
{
    if (!s_ble_state.initialized || !s_ble_state.scanning) {
        return false;
    }
    
    ESP_LOGI(TAG, "Stopping BLE scan");
    
    int rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop scan: %d", rc);
        return false;
    }
    
    s_ble_state.scanning = false;
    ESP_LOGI(TAG, "BLE scan stopped");
    return true;
}

bool ble_manager_connect(const uint8_t *addr)
{
    if (!s_ble_state.initialized) {
        ESP_LOGE(TAG, "BLE manager not initialized");
        return false;
    }
    
    if (addr == NULL) {
        ESP_LOGE(TAG, "Invalid address");
        return false;
    }
    
    if (s_ble_state.connected) {
        ESP_LOGW(TAG, "Already connected");
        return false;
    }

    if (s_ble_state.advertising) {
        ble_manager_stop_advertising();
    }
    
    ESP_LOGI(TAG, "Connecting to device: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    
    // 停止扫描
    if (s_ble_state.scanning) {
        ble_manager_stop_scan();
    }
    
    // 创建连接
    ble_addr_t peer_addr = {
        .type = BLE_ADDR_PUBLIC,
    };
    memcpy(peer_addr.val, addr, 6);
    
    // Default connection parameters
    struct ble_gap_conn_params conn_params = {
        .scan_itvl = 16,    // x 0.625ms = 10ms
        .scan_window = 16,  // x 0.625ms = 10ms
        .itvl_min = 24,     // x 1.25ms = 30ms
        .itvl_max = 40,     // x 1.25ms = 50ms
        .latency = 0,
        .supervision_timeout = 200, // x 10ms = 2000ms
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer_addr, 30000, &conn_params, 
                             ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
        return false;
    }
    
    ESP_LOGI(TAG, "Connection initiated");
    return true;
}

bool ble_manager_disconnect(void)
{
    if (!s_ble_state.connected) {
        return false;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device");
    
    int rc = ble_gap_terminate(s_ble_state.connected_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to disconnect: %d", rc);
        return false;
    }
    
    return true;
}

int ble_manager_send_data(const uint8_t *data, uint16_t length)
{
    // EPD device is GATT server; phone writes to us, we don't write to phone
    ESP_LOGW(TAG, "send_data not supported in server mode");
    (void)data;
    (void)length;
    return -1;
}

bool ble_manager_is_connected(void)
{
    return s_ble_state.connected;
}

bool ble_manager_get_connected_device(uint8_t *addr)
{
    if (!s_ble_state.connected || addr == NULL) {
        return false;
    }
    
    memcpy(addr, s_ble_state.connected_addr, 6);
    return true;
}
