/**
 * @file ble_reader_screen.h
 * @brief 蓝牙读书屏幕 - 用于通过蓝牙接收和显示书籍位图内容
 * 
 * 采用分页位图显示，支持滑动窗口缓存和预加载
 * 支持双模式：阅读模式 + 传输模式
 */

#ifndef BLE_READER_SCREEN_H
#define BLE_READER_SCREEN_H

#include "screen_manager.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief BLE 工作模式
 */
typedef enum {
    BLE_MODE_READING = 0,    // 阅读模式（默认）- 允许 littlefs，拒绝 SD 卡写入
    BLE_MODE_TRANSFER = 1,   // 传输模式 - 允许 SD 卡和 littlefs 写入
} ble_work_mode_t;

/**
 * @brief 蓝牙读书屏幕状态
 */
typedef enum {
    BLE_READER_STATE_IDLE,           // 空闲状态
    BLE_READER_STATE_WAITING,        // 等待连接/传输
    BLE_READER_STATE_CONNECTING,     // 连接中
    BLE_READER_STATE_CONNECTED,      // 已连接
    BLE_READER_STATE_RECEIVING,      // 接收数据中
    BLE_READER_STATE_READING,        // 阅读中
    BLE_READER_STATE_READY,          // 传输完成，就绪状态
} ble_reader_state_t;

/**
 * @brief 设置 BLE 工作模式
 * @param mode 工作模式 (BLE_MODE_READING 或 BLE_MODE_TRANSFER)
 */
void ble_reader_set_mode(ble_work_mode_t mode);

/**
 * @brief 获取当前 BLE 工作模式
 * @return 当前工作模式
 */
ble_work_mode_t ble_reader_get_mode(void);

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
