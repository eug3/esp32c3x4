/**
 * @file ble_cache_manager.h
 * @brief 蓝牙页面缓存管理器 - 管理LittleFS中的位图缓存
 *
 * 采用滑动窗口机制：
 * - 预加载机制：当接近末尾时自动请求更多页面
 * - 清理机制：超出窗口的旧页面自动删除
 */

#ifndef BLE_CACHE_MANAGER_H
#define BLE_CACHE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化缓存管理器
 * @return true 成功，false 失败
 */
bool ble_cache_init(void);

/**
 * @brief 反初始化缓存管理器
 */
void ble_cache_deinit(void);

/**
 * @brief 预加载回调函数
 * 当需要预加载更多页面时调用
 * @param book_id 书籍ID
 * @param start_page 起始页码
 * @param page_count 所需页数
 */
typedef void (*ble_cache_preload_cb)(uint16_t book_id, uint16_t start_page, uint8_t page_count);

/**
 * @brief 注册预加载回调
 * @param cb 回调函数
 */
void ble_cache_register_preload_cb(ble_cache_preload_cb cb);

/**
 * @brief 更新当前阅读位置，触发预加载逻辑
 * 当阅读位置接近已缓存范围末尾时，自动触发预加载
 * @param book_id 书籍ID
 * @param current_page 当前页码
 * @return true 如果触发了预加载，false 否则
 */
bool ble_cache_update_read_position(uint16_t book_id, uint16_t current_page);

#endif // BLE_CACHE_MANAGER_H
