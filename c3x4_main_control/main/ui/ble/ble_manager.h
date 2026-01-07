/**
 * @file ble_manager.h
 * @brief 蓝牙管理器 - 处理蓝牙通信的核心功能
 * 
 * 使用 NimBLE（ESP32 的轻量级蓝牙栈）来实现蓝牙功能
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 蓝牙设备信息
 */
typedef struct {
    uint8_t addr[6];            // MAC地址
    int8_t rssi;                // 信号强度
    char name[32];              // 设备名称
    uint16_t name_len;          // 名称长度

    // If present in advertisement (AD type 0x06/0x07), first 128-bit service UUID (little-endian).
    bool has_service_uuid128;
    uint8_t service_uuid128_le[16];
} ble_device_info_t;

/**
 * @brief 蓝牙设备回调函数
 * 当发现新设备时调用
 */
typedef void (*ble_on_device_found_cb)(const ble_device_info_t *device);

/**
 * @brief 蓝牙连接状态回调函数
 */
typedef void (*ble_on_connect_cb)(bool connected);

/**
 * @brief 蓝牙数据接收回调函数
 */
typedef void (*ble_on_data_received_cb)(const uint8_t *data, uint16_t length);

/**
 * @brief 初始化蓝牙管理器
 * @return true 成功，false 失败
 */
bool ble_manager_init(void);

/**
 * @brief 反初始化蓝牙管理器
 */
void ble_manager_deinit(void);

/**
 * @brief 注册设备发现回调
 * @param cb 回调函数指针
 */
void ble_manager_register_device_found_cb(ble_on_device_found_cb cb);

/**
 * @brief 注册连接状态回调
 * @param cb 回调函数指针
 */
void ble_manager_register_connect_cb(ble_on_connect_cb cb);

/**
 * @brief 注册数据接收回调
 * @param cb 回调函数指针
 */
void ble_manager_register_data_received_cb(ble_on_data_received_cb cb);

/**
 * @brief 开始广播并等待手机连接
 * @return true 成功，false 失败
 */
bool ble_manager_start_advertising(void);

/**
 * @brief 停止广播
 * @return true 成功或已停止，false 失败
 */
bool ble_manager_stop_advertising(void);

/**
 * @brief 启动蓝牙扫描
 * @param duration_ms 扫描持续时间（毫秒），0 表示无限扫描
 * @return true 成功，false 失败
 */
bool ble_manager_start_scan(uint32_t duration_ms);

/**
 * @brief 停止蓝牙扫描
 * @return true 成功，false 失败
 */
bool ble_manager_stop_scan(void);

/**
 * @brief 连接到指定的蓝牙设备
 * @param addr 设备蓝牙地址（6字节）
 * @return true 成功，false 失败
 */
bool ble_manager_connect(const uint8_t *addr);

/**
 * @brief Set target 128-bit service UUID (little-endian) discovered from advertisement.
 * Must be called before ble_manager_connect() for dynamic UUID exchange.
 */
void ble_manager_set_target_service_uuid128_le(const uint8_t uuid_le[16]);

/**
 * @brief 断开蓝牙连接
 * @return true 成功，false 失败
 */
bool ble_manager_disconnect(void);

/**
 * @brief 发送数据到连接的设备
 * @param data 数据指针
 * @param length 数据长度
 * @return 实际发送的字节数，-1 表示错误
 */
int ble_manager_send_data(const uint8_t *data, uint16_t length);

/**
 * @brief 检查蓝牙是否已连接
 * @return true 已连接，false 未连接
 */
bool ble_manager_is_connected(void);

/**
 * @brief 获取已连接的设备地址
 * @param addr 用于存储设备地址的缓冲区（必须至少6字节）
 * @return true 成功获取，false 未连接或出错
 */
bool ble_manager_get_connected_device(uint8_t *addr);

#endif // BLE_MANAGER_H
