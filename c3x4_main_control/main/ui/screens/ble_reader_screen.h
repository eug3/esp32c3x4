/**
 * @file ble_reader_screen.h
 * @brief 蓝牙读书屏幕 - 用于通过蓝牙接收和显示书籍位图内容
 * 
 * 采用分页位图显示，支持滑动窗口缓存和预加载
 */

#ifndef BLE_READER_SCREEN_H
#define BLE_READER_SCREEN_H

#include "screen_manager.h"
#include <stdint.h>

/**
 * @brief 蓝牙读书屏幕状态
 */
typedef enum {
    BLE_READER_STATE_IDLE,           // 空闲状态
    BLE_READER_STATE_CONNECTING,     // 连接中
    BLE_READER_STATE_CONNECTED,      // 已连接
    BLE_READER_STATE_SCANNING,       // 扫描设备中
    BLE_READER_STATE_RECEIVING,      // 接收数据中
    BLE_READER_STATE_READING,        // 阅读中
} ble_reader_state_t;

/**
 * @brief 初始化蓝牙读书屏幕
 */
void ble_reader_screen_init(void);

/**
 * @brief 获取蓝牙读书屏幕实例
 * @return 屏幕指针
 */
screen_t* ble_reader_screen_get_instance(void);

/**
 * @brief 获取当前蓝牙读书屏幕状态
 * @return 当前状态
 */
ble_reader_state_t ble_reader_screen_get_state(void);

/**
 * @brief 启动蓝牙扫描
 */
void ble_reader_screen_start_scan(void);

/**
 * @brief 停止蓝牙扫描
 */
void ble_reader_screen_stop_scan(void);

/**
 * @brief 连接到蓝牙设备
 * @param addr 设备蓝牙地址
 * @return true 成功，false 失败
 */
bool ble_reader_screen_connect_device(const uint8_t *addr);

/**
 * @brief 断开蓝牙连接
 */
void ble_reader_screen_disconnect(void);

/**
 * @brief 设置当前显示的书籍ID
 * @param book_id 书籍ID
 */
void ble_reader_screen_set_current_book(uint16_t book_id);

/**
 * @brief 跳转到指定页码
 * @param page_num 页码
 */
void ble_reader_screen_goto_page(uint16_t page_num);

/**
 * @brief 下一页
 */
void ble_reader_screen_next_page(void);

/**
 * @brief 上一页
 */
void ble_reader_screen_prev_page(void);

#endif // BLE_READER_SCREEN_H
