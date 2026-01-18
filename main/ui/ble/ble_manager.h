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
 * @brief 发送通知给已连接的客户端（通过 CMD 特征）
 * @param data 通知数据指针
 * @param length 数据长度
 * @return true 成功，false 失败
 */
bool ble_manager_send_notification(const uint8_t *data, uint16_t length);

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
