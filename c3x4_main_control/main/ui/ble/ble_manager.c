/**
 * @file ble_manager.c
 * @brief 蓝牙管理器实现 - 基于 monster_c3x4 的 NimBLE SPP 服务器
 *
 * 参考 monster_c3x4 项目的蓝牙实现
 */

#include "ble_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include <string.h>

static const char *TAG = "BLE_MANAGER";

// SPP Service UUID
#define BLE_SVC_SPP_UUID16          0xABF0
#define BLE_SVC_SPP_CHR_UUID16      0xABF1

// 设备名称
#define DEVICE_NAME                 "MFP-EPD"

// BLE 管理器状态
static struct {
    bool initialized;
    bool advertising;
    bool connected;
    uint16_t conn_handle;
    uint8_t own_addr_type;
    bool subscribed;  // 客户端是否订阅了通知
    uint16_t spp_handle;  // SPP characteristic value handle
    
    // 回调函数
    ble_on_connect_cb connect_cb;
    ble_on_data_received_cb data_received_cb;
} s_ble = {0};

// Forward declarations
static void ble_spp_server_advertise(void);
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg);
static void ble_spp_server_on_sync(void);
static void ble_spp_server_on_reset(int reason);
static void ble_spp_server_host_task(void *param);
static int ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, 
                                struct ble_gatt_access_ctxt *ctxt, void *arg);


static void print_memory( const char* TAG ,const char* when) {
    ESP_LOGI(TAG, "%s: Free %luKB, Min %luKB, Largest %luB",
        when, esp_get_free_heap_size()/1024,
        esp_get_minimum_free_heap_size()/1024,
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
void ble_store_config_init(void);
/**
 * 日志连接信息
 */
static void ble_spp_server_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    ESP_LOGI(TAG, "handle=%d our_ota_addr_type=%d", desc->conn_handle, desc->our_ota_addr.type);
    ESP_LOGI(TAG, " our_id_addr_type=%d", desc->our_id_addr.type);
    ESP_LOGI(TAG, " peer_ota_addr_type=%d", desc->peer_ota_addr.type);
    ESP_LOGI(TAG, " peer_id_addr_type=%d", desc->peer_id_addr.type);
    ESP_LOGI(TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                  "encrypted=%d authenticated=%d bonded=%d",
                  desc->conn_itvl, desc->conn_latency,
                  desc->supervision_timeout,
                  desc->sec_state.encrypted,
                  desc->sec_state.authenticated,
                  desc->sec_state.bonded);
}

/**
 * 开始广播
 */
static void ble_spp_server_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;
    

    memset(&fields, 0, sizeof(fields));

    /* 广播标志: 一般可发现 + 仅 BLE (不支持 BR/EDR) */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* TX power level */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* 设备名称 */
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    /* 16-bit Service UUIDs */
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(BLE_SVC_SPP_UUID16)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
     
    
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
         
         
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    /* 开始广播 */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    print_memory( TAG ,"Before Start Advertising");     
    rc = ble_gap_adv_start(s_ble.own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_spp_server_gap_event, NULL);
    print_memory( TAG ,"After Start Advertising");
    if (rc != 0) {
        
        
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
        return;
    }

    s_ble.advertising = true;
    ESP_LOGI(TAG, "Advertising started (name=%s)", DEVICE_NAME);
}

/**
 * GAP 事件处理
 */
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* 新连接建立或连接失败 */
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                ble_spp_server_print_conn_desc(&desc);
            }
            
            s_ble.connected = true;
            s_ble.conn_handle = event->connect.conn_handle;
            s_ble.advertising = false;
            
            // 调用连接回调
            if (s_ble.connect_cb != NULL) {
                s_ble.connect_cb(true);
            }
        } else {
            // 连接失败，继续广播
            s_ble.connected = false;
            ble_spp_server_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        ble_spp_server_print_conn_desc(&event->disconnect.conn);

        s_ble.connected = false;
        s_ble.subscribed = false;
        s_ble.conn_handle = 0;

        // 调用连接回调
        if (s_ble.connect_cb != NULL) {
            s_ble.connect_cb(false);
        }

        /* 连接断开，恢复广播 */
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* 连接参数更新 */
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc == 0) {
            ble_spp_server_print_conn_desc(&desc);
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.channel_id,
                 event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d "
                 "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify,
                 event->subscribe.cur_notify,
                 event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);
        
        s_ble.subscribed = (event->subscribe.cur_notify || event->subscribe.cur_indicate);
        return 0;

    default:
        return 0;
    }
}

/**
 * GATT 服务特征处理回调
 */
static int ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Callback for read");
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "Data received in write event, conn_handle=%x, attr_handle=%x",
                 conn_handle, attr_handle);
        
        // 提取接收到的数据
        if (s_ble.data_received_cb != NULL && ctxt->om != NULL) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > 0) {
                uint8_t *buf = (uint8_t *)malloc(om_len);
                if (buf != NULL) {
                    int copied = os_mbuf_copydata(ctxt->om, 0, om_len, buf);
                    if (copied == 0) {
                        s_ble.data_received_cb(buf, om_len);
                    }
                    free(buf);
                }
            }
        }
        break;

    default:
        ESP_LOGI(TAG, "Default callback, op=%d", ctxt->op);
        break;
    }
    return 0;
}

/**
 * 定义 SPP GATT 服务
 */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* SPP Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* SPP characteristic */
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_CHR_UUID16),
                .access_cb = ble_svc_gatt_handler,
                .val_handle = &s_ble.spp_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services */
    },
};

/**
 * GATT 注册回调
 */
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registering characteristic %s with def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

/**
 * 初始化 GATT 服务器
 */
static int gatt_svr_init(void)
{
    int rc;
    
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * BLE 栈重置回调
 */
static void ble_spp_server_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

/**
 * BLE 栈同步回调
 */
static void ble_spp_server_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address; rc=%d", rc);
        return;
    }

    /* 确定广播时使用的地址类型（暂不使用隐私） */
    rc = ble_hs_id_infer_auto(0, &s_ble.own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    /* 打印设备地址 */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(s_ble.own_addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Device Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }

    /* 开始广播 */
    ble_spp_server_advertise();
}

/**
 * BLE 主机任务
 */
static void ble_spp_server_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    
    /* 此函数会一直运行直到 nimble_port_stop() 被调用 */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

/**********************
 * 公共 API 函数
 **********************/

bool ble_manager_init(void)
{
    if (s_ble.initialized) {
        ESP_LOGW(TAG, "BLE manager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE manager...");

    /* 注意：NVS flash 应该在 app_main 中已经初始化，这里不再重复初始化 */

    /* 初始化 NimBLE port */
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble %d", ret);
        return false;
    }

    /* 初始化 NimBLE 主机配置 */
    ble_hs_cfg.reset_cb = ble_spp_server_on_reset;
    ble_hs_cfg.sync_cb = ble_spp_server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* 注册自定义 GATT 服务 */
    int rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init GATT server; rc=%d", rc);
        return false;
    }

    /* 设置设备名称 */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name; rc=%d", rc);
    }

    /* 初始化 BLE 存储配置（用于配对等） */
    ble_store_config_init();

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(ble_spp_server_host_task);

    s_ble.initialized = true;
    ESP_LOGI(TAG, "BLE manager initialized successfully");

    return true;
}

void ble_manager_deinit(void)
{
    if (!s_ble.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing BLE manager...");

    if (s_ble.advertising) {
        ble_manager_stop_advertising();
    }

    if (s_ble.connected) {
        ble_manager_disconnect();
    }

    nimble_port_deinit();

    s_ble.initialized = false;
    ESP_LOGI(TAG, "BLE manager deinitialized");
}

void ble_manager_register_connect_cb(ble_on_connect_cb cb)
{
    s_ble.connect_cb = cb;
}

void ble_manager_register_data_received_cb(ble_on_data_received_cb cb)
{
    s_ble.data_received_cb = cb;
}

void ble_manager_register_device_found_cb(ble_on_device_found_cb cb)
{
    // 不支持扫描功能
    (void)cb;
}

bool ble_manager_start_advertising(void)
{
    if (!s_ble.initialized) {
        ESP_LOGE(TAG, "BLE manager not initialized");
        return false;
    }

    if (s_ble.advertising) {
        ESP_LOGW(TAG, "Already advertising");
        return true;
    }

    ble_spp_server_advertise();
    return true;
}

bool ble_manager_stop_advertising(void)
{
    if (!s_ble.initialized || !s_ble.advertising) {
        return true;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop advertising; rc=%d", rc);
        return false;
    }

    s_ble.advertising = false;
    ESP_LOGI(TAG, "Advertising stopped");
    return true;
}

bool ble_manager_start_scan(uint32_t duration_ms)
{
    // 不支持扫描功能
    (void)duration_ms;
    ESP_LOGW(TAG, "Scan not supported in server mode");
    return false;
}

bool ble_manager_stop_scan(void)
{
    // 不支持扫描功能
    return false;
}

bool ble_manager_connect(const uint8_t *addr)
{
    // 不支持主动连接（作为服务器）
    (void)addr;
    ESP_LOGW(TAG, "Connect not supported in server mode");
    return false;
}

void ble_manager_set_target_service_uuid128_le(const uint8_t uuid_le[16])
{
    // 不支持
    (void)uuid_le;
}

bool ble_manager_disconnect(void)
{
    if (!s_ble.connected) {
        return false;
    }

    ESP_LOGI(TAG, "Disconnecting from device");

    int rc = ble_gap_terminate(s_ble.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to disconnect; rc=%d", rc);
        return false;
    }

    return true;
}

int ble_manager_send_data(const uint8_t *data, uint16_t length)
{
    if (!s_ble.connected || !s_ble.subscribed) {
        ESP_LOGW(TAG, "Not connected or client not subscribed");
        return -1;
    }

    if (data == NULL || length == 0) {
        return -1;
    }

    /* 发送通知 */
    struct os_mbuf *txom = ble_hs_mbuf_from_flat(data, length);
    if (txom == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return -1;
    }

    int rc = ble_gatts_notify_custom(s_ble.conn_handle, s_ble.spp_handle, txom);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error sending notification; rc=%d", rc);
        return -1;
    }

    ESP_LOGI(TAG, "Notification sent successfully, length=%d", length);
    return length;
}

bool ble_manager_is_connected(void)
{
    return s_ble.connected;
}

bool ble_manager_get_connected_device(uint8_t *addr)
{
    if (!s_ble.connected || addr == NULL) {
        return false;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(s_ble.conn_handle, &desc);
    if (rc != 0) {
        return false;
    }

    memcpy(addr, desc.peer_id_addr.val, 6);
    return true;
}
