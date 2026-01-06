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
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/host/include/host/ble_hs.h"
#include "nimble/host/include/host/ble_gap.h"
#include "nimble/host/include/host/ble_gatt.h"
#include "nimble/host/include/host/ble_gattc.h"
#include "nimble/host/include/os/os_mbuf.h"
#include "nimble/host/include/host/ble_uuid.h"
#include "nimble/host/include/services/gap/ble_svc_gap.h"
#include "nimble/host/include/services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_MANAGER";

// 蓝牙管理器状态
static struct {
    bool initialized;
    bool scanning;
    bool connected;
    uint8_t connected_addr[6];
    uint16_t connected_handle;

    // Dynamic UUID exchange: selected service UUID from advertisement (little-endian bytes).
    bool target_uuid_valid;
    ble_uuid128_t target_uuid;

    // Discovered IO characteristic (WRITE+NOTIFY) handles.
    bool gatt_ready;
    uint16_t svc_start_handle;
    uint16_t svc_end_handle;
    uint16_t io_val_handle;
    uint16_t io_cccd_handle;
    
    // 回调函数
    ble_on_device_found_cb device_found_cb;
    ble_on_connect_cb connect_cb;
    ble_on_data_received_cb data_received_cb;
} s_ble_state = {
    .initialized = false,
    .scanning = false,
    .connected = false,
    .device_found_cb = NULL,
    .connect_cb = NULL,
    .data_received_cb = NULL,
    .target_uuid_valid = false,
    .gatt_ready = false,
    .svc_start_handle = 0,
    .svc_end_handle = 0,
    .io_val_handle = 0,
    .io_cccd_handle = 0,
};

static void reset_gatt_state(void)
{
    s_ble_state.gatt_ready = false;
    s_ble_state.svc_start_handle = 0;
    s_ble_state.svc_end_handle = 0;
    s_ble_state.io_val_handle = 0;
    s_ble_state.io_cccd_handle = 0;
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

static int gatt_disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *arg);
static int gatt_disc_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg);
static int gatt_disc_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);

static void maybe_enable_notify(uint16_t conn_handle)
{
    if (!s_ble_state.io_cccd_handle || !s_ble_state.io_val_handle) {
        return;
    }

    // CCCD: 0x0001 enable notifications
    uint8_t cccd_val[2] = {0x01, 0x00};
    int rc = ble_gattc_write_flat(conn_handle, s_ble_state.io_cccd_handle, cccd_val, sizeof(cccd_val), NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to write CCCD rc=%d", rc);
        return;
    }

    s_ble_state.gatt_ready = true;
    ESP_LOGI(TAG, "GATT ready: io_val_handle=%u cccd_handle=%u", s_ble_state.io_val_handle, s_ble_state.io_cccd_handle);
}

static int gatt_disc_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)arg;
    if (error && error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            maybe_enable_notify(conn_handle);
        }
        return 0;
    }

    if (dsc) {
        // CCCD UUID 0x2902
        const ble_uuid16_t cccd = BLE_UUID16_INIT(0x2902);
        if (ble_uuid_cmp(&dsc->uuid.u, &cccd.u) == 0) {
            s_ble_state.io_cccd_handle = dsc->handle;
        }
    }
    return 0;
}

static int gatt_disc_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error && error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            // Start descriptor discovery for the chosen characteristic
            if (s_ble_state.io_val_handle && s_ble_state.svc_end_handle) {
                int rc = ble_gattc_disc_all_dscs(conn_handle, s_ble_state.io_val_handle, s_ble_state.svc_end_handle, gatt_disc_dsc_cb, NULL);
                ESP_LOGI(TAG, "Disc dscs rc=%d", rc);
            }
        }
        return 0;
    }

    if (chr) {
        // Need a characteristic that can WRITE and NOTIFY.
        bool can_write = (chr->properties & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)) != 0;
        bool can_notify = (chr->properties & (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) != 0;
        if (can_write && can_notify && s_ble_state.io_val_handle == 0) {
            s_ble_state.io_val_handle = chr->val_handle;
            ESP_LOGI(TAG, "Selected IO characteristic val_handle=%u props=0x%02x", chr->val_handle, chr->properties);
        }
    }

    return 0;
}

static int gatt_disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (error && error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            if (s_ble_state.svc_start_handle && s_ble_state.svc_end_handle) {
                int rc = ble_gattc_disc_all_chrs(conn_handle, s_ble_state.svc_start_handle, s_ble_state.svc_end_handle, gatt_disc_chr_cb, NULL);
                ESP_LOGI(TAG, "Disc chrs rc=%d", rc);
            } else {
                ESP_LOGW(TAG, "Target service not found on peer");
            }
        }
        return 0;
    }

    if (service) {
        s_ble_state.svc_start_handle = service->start_handle;
        s_ble_state.svc_end_handle = service->end_handle;
        ESP_LOGI(TAG, "Found target service: start=%u end=%u", service->start_handle, service->end_handle);
    }
    return 0;
}

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
                uint8_t *data = event->disc.data;
                uint8_t data_len = event->disc.data_len;
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

                // Parse first advertised 128-bit service UUID (for dynamic UUID exchange).
                parse_adv_for_uuid128(event->disc.data, event->disc.data_len, &device_info);
                
                s_ble_state.device_found_cb(&device_info);
            }
            break;
            
        case BLE_GAP_EVENT_CONNECT:
            // 连接成功
            ESP_LOGI(TAG, "Device connected");
            s_ble_state.connected = true;
            s_ble_state.connected_handle = event->connect.conn_handle;
            memcpy(s_ble_state.connected_addr, event->connect.peer_id_addr.val, 6);
            reset_gatt_state();

            // Start GATT discovery for the dynamically exchanged service UUID.
            if (s_ble_state.target_uuid_valid) {
                int rc = ble_gattc_disc_svc_by_uuid(s_ble_state.connected_handle, &s_ble_state.target_uuid.u, gatt_disc_svc_cb, NULL);
                ESP_LOGI(TAG, "Disc svc by uuid rc=%d", rc);
            } else {
                ESP_LOGW(TAG, "No target service UUID set; will not start GATT discovery");
            }
            
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
            
            if (s_ble_state.connect_cb != NULL) {
                s_ble_state.connect_cb(false);
            }
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
    
    // 初始化NimBLE
    ESP_ERROR_CHECK(nimble_port_init());
    
    // 配置BLE GAP
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    // 设置本地设备名称
    ble_svc_gap_device_name_set("Monster-BLE");
    
    // 注册GAP事件处理
    ble_gap_set_event_cb(ble_gap_event_handler, NULL);
    
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
    
    if (s_ble_state.scanning) {
        ble_manager_stop_scan();
    }
    
    if (s_ble_state.connected) {
        ble_manager_disconnect();
    }
    
    // NimBLE 清理
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

bool ble_manager_start_scan(uint32_t duration_ms)
{
    if (!s_ble_state.initialized) {
        ESP_LOGE(TAG, "BLE manager not initialized");
        return false;
    }
    
    if (s_ble_state.scanning) {
        ESP_LOGW(TAG, "Already scanning");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting BLE scan");
    
    // 设置扫描参数
    struct ble_gap_disc_params disc_params = {
        .type = BLE_GAP_DISC_TYPE_PASSIVE,
        .filter = BLE_GAP_DISC_FILTER_NONE,
        .passive_on_disc = false,
        .limited = false,
        .itvl = BLE_GAP_LIM_DISC_ITVL_MIN,
        .window = BLE_GAP_LIM_DISC_WIN,
        .filter_duplicates = false,
    };
    
    int rc = ble_gap_disc(BLE_GAP_ADDR_TYPE_PUBLIC, duration_ms, &disc_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan: %d", rc);
        return false;
    }
    
    s_ble_state.scanning = true;
    ESP_LOGI(TAG, "BLE scan started");
    return true;
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
    
    ESP_LOGI(TAG, "Connecting to device: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    if (!s_ble_state.target_uuid_valid) {
        ESP_LOGW(TAG, "Target service UUID not set; connection may be useless");
    }
    
    // 停止扫描
    if (s_ble_state.scanning) {
        ble_manager_stop_scan();
    }
    
    // 创建连接
    ble_addr_t peer_addr = {
        .type = BLE_ADDR_PUBLIC,
    };
    memcpy(peer_addr.val, addr, 6);
    
    int rc = ble_gap_connect(BLE_GAP_ADDR_TYPE_PUBLIC, &peer_addr, 10000, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
        return false;
    }
    
    ESP_LOGI(TAG, "Connection initiated");
    return true;
}

void ble_manager_set_target_service_uuid128_le(const uint8_t uuid_le[16])
{
    if (uuid_le == NULL) {
        s_ble_state.target_uuid_valid = false;
        return;
    }
    s_ble_state.target_uuid.u.type = BLE_UUID_TYPE_128;
    memcpy(s_ble_state.target_uuid.value, uuid_le, 16);
    s_ble_state.target_uuid_valid = true;
    ESP_LOGI(TAG, "Target service UUID set (LE): %02x%02x%02x%02x...",
             uuid_le[0], uuid_le[1], uuid_le[2], uuid_le[3]);
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
    if (!s_ble_state.connected) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }
    
    if (data == NULL || length == 0) {
        ESP_LOGE(TAG, "Invalid data");
        return -1;
    }
    
    if (!s_ble_state.gatt_ready || s_ble_state.io_val_handle == 0) {
        ESP_LOGW(TAG, "GATT not ready; drop send len=%u", length);
        return -1;
    }

    // Write request to IO characteristic.
    int rc = ble_gattc_write_flat(s_ble_state.connected_handle, s_ble_state.io_val_handle, data, length, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_write_flat failed rc=%d", rc);
        return -1;
    }

    ESP_LOGD(TAG, "Data write queued: %u bytes", length);
    return length;
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
