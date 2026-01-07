/**
 * @file ble_cache_manager.h
 * @brief 蓝牙页面缓存管理器 - 管理LittleFS中的位图缓存
 * 
 * 采用滑动窗口机制：
 * - 预加载机制：当接近末尾时自动请求更多页面
 * - 清理机制：超出窗口的旧页面自动删除
 * - 内存映射：频繁访问的页面保留在内存中加速访问
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
 * @brief 保存页面数据到LittleFS
 * @param book_id 书籍ID
 * @param page_num 页码
 * @param data 位图数据（48KB）
 * @param size 数据大小
 * @return true 成功，false 失败
 */
bool ble_cache_save_page(uint16_t book_id, uint16_t page_num, 
                         const uint8_t *data, uint32_t size);

/**
 * @brief 从缓存读取页面数据
 * @param book_id 书籍ID
 * @param page_num 页码
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 读取的字节数，0表示页面不存在或读取失败
 */
uint32_t ble_cache_load_page(uint16_t book_id, uint16_t page_num,
                             uint8_t *buffer, uint32_t buffer_size);

/**
 * @brief 检查页面是否已缓存
 * @param book_id 书籍ID
 * @param page_num 页码
 * @return true 已缓存，false 未缓存
 */
bool ble_cache_page_exists(uint16_t book_id, uint16_t page_num);

/**
 * @brief 获取已缓存的最小页码
 * @param book_id 书籍ID
 * @return 最小页码，-1 表示没有缓存
 */
int32_t ble_cache_get_min_page(uint16_t book_id);

/**
 * @brief 获取已缓存的最大页码
 * @param book_id 书籍ID
 * @return 最大页码，-1 表示没有缓存
 */
int32_t ble_cache_get_max_page(uint16_t book_id);

/**
 * @brief 清理指定页码范围之外的缓存
 * 用于滑动窗口的清理，保留指定范围内的页面
 * @param book_id 书籍ID
 * @param min_page 最小保留页码
 * @param max_page 最大保留页码
 * @return 删除的页面数
 */
uint32_t ble_cache_cleanup_outside_range(uint16_t book_id, 
                                         uint16_t min_page, 
                                         uint16_t max_page);

/**
 * @brief 清空特定书籍的所有缓存
 * @param book_id 书籍ID
 * @return 删除的页面数
 */
uint32_t ble_cache_clear_book(uint16_t book_id);

/**
 * @brief 清空所有缓存
 * @return 删除的页面数
 */
uint32_t ble_cache_clear_all(void);

/**
 * @brief 获取缓存统计信息
 */
typedef struct {
    uint32_t total_cached_pages;    // 总缓存页数
    uint32_t total_size_bytes;      // 总大小（字节）
    uint32_t free_space_bytes;      // LittleFS剩余空间
} ble_cache_stats_t;

/**
 * @brief 获取缓存统计信息
 * @param stats 输出统计结构体
 * @return true 成功，false 失败
 */
bool ble_cache_get_stats(ble_cache_stats_t *stats);

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
