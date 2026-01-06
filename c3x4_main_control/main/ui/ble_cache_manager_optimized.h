/**
 * @file ble_cache_manager_optimized.h
 * @brief ESP32 端 BLE 缓存管理 - 滑动窗口 + 预加载优化
 * 
 * 设计目标：
 * 1. 实现 5-10 页的滑动窗口缓存
 * 2. 智能预加载机制（后台任务）
 * 3. LittleFS 存储管理（480KB）
 * 4. 内存优化（使用 PSRAM 如可用）
 */

#ifndef BLE_CACHE_MANAGER_OPTIMIZED_H
#define BLE_CACHE_MANAGER_OPTIMIZED_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 缓存配置
 */
typedef struct {
    uint16_t max_cached_pages;        // 最大缓存页数（通常 10）
    uint32_t cache_size_bytes;        // 总缓存大小（480KB）
    uint32_t page_size_bytes;         // 单页大小（48KB）
    
    // 窗口策略
    uint16_t window_size;             // 窗口大小（5-10 页）
    uint16_t prefetch_threshold;      // 预加载阈值（3-5 页）
    uint32_t prefetch_delay_ms;       // 预加载延迟（毫秒）
    
    // 过期策略
    uint32_t page_ttl_seconds;        // 页面 TTL（3600 秒 = 1 小时）
    bool use_psram;                   // 是否使用 PSRAM
} ble_cache_config_t;

/**
 * @brief 缓存中的页面元数据
 */
typedef struct {
    uint16_t book_id;                 // 书籍 ID
    uint16_t page_num;                // 页码
    char filename[64];                // LittleFS 文件名
    bool valid;                       // 数据是否有效
    uint32_t timestamp;               // 缓存时间戳（秒）
    uint32_t size_bytes;              // 页面大小
    uint32_t access_count;            // 访问计数（用于 LRU）
    uint32_t last_access_time;        // 最后访问时间
} ble_cached_page_t;

/**
 * @brief 滑动窗口状态
 */
typedef struct {
    uint16_t window_start;            // 窗口起始页
    uint16_t window_end;              // 窗口末尾页
    uint16_t current_page;            // 当前显示页（由 UI 更新）
    uint32_t last_update_time;        // 窗口最后更新时间
    
    // 预加载状态
    uint16_t pending_requests;        // 待处理的预加载请求数
    bool prefetch_active;             // 预加载是否进行中
    
    // 统计
    uint32_t cache_hits;              // 缓存命中次数
    uint32_t cache_misses;            // 缓存未命中次数
} ble_sliding_window_t;

/**
 * @brief 接收状态（用于页面分片重组）
 */
typedef struct {
    uint32_t bytes_received;          // 已接收字节数
    uint32_t total_bytes;             // 总字节数
    uint32_t offset;                  // 当前分片偏移
    uint32_t chunk_size;              // 本分片大小
    uint32_t last_packet_time;        // 最后包时间（超时检测）
    bool in_progress;                 // 接收是否进行中
    uint32_t start_time;              // 接收开始时间
} ble_rx_state_t;

/**
 * @brief 初始化缓存管理器
 * @param config 配置参数
 * @return true 成功，false 失败
 */
bool ble_cache_manager_init(const ble_cache_config_t* config);

/**
 * @brief 反初始化缓存管理器
 */
void ble_cache_manager_deinit(void);

/**
 * @brief 更新滑动窗口（由 UI 调用）
 * @param current_page 当前显示的页码
 */
void ble_cache_update_window(uint16_t current_page);

/**
 * @brief 检查页面是否已缓存
 * @param book_id 书籍 ID
 * @param page_num 页码
 * @return true 已缓存，false 未缓存
 */
bool ble_cache_page_exists(uint16_t book_id, uint16_t page_num);

/**
 * @brief 从缓存中读取页面
 * @param book_id 书籍 ID
 * @param page_num 页码
 * @param[out] buffer 输出缓冲区（调用者分配）
 * @param max_len 缓冲区大小
 * @return 实际读取字节数，-1 表示失败
 */
int32_t ble_cache_read_page(uint16_t book_id, uint16_t page_num, uint8_t* buffer, uint32_t max_len);

/**
 * @brief 向缓存写入页面（追加式，用于分片重组）
 * @param book_id 书籍 ID
 * @param page_num 页码
 * @param offset 页面中的偏移（用于分片）
 * @param data 数据指针
 * @param len 数据长度
 * @param total_size 页面总大小
 * @return true 成功（包括分片完成），false 失败
 */
bool ble_cache_write_page_chunk(
    uint16_t book_id,
    uint16_t page_num,
    uint32_t offset,
    const uint8_t* data,
    uint32_t len,
    uint32_t total_size
);

/**
 * @brief 清空缓存中过期的页面
 * @return 清理的页面数
 */
uint32_t ble_cache_cleanup_expired(void);

/**
 * @brief 获取缓存统计信息
 * @param[out] hits 命中数
 * @param[out] misses 未命中数
 * @param[out] pages_cached 已缓存页数
 */
void ble_cache_get_stats(uint32_t* hits, uint32_t* misses, uint32_t* pages_cached);

/**
 * @brief 获取滑动窗口状态
 * @return 窗口状态结构体
 */
ble_sliding_window_t ble_cache_get_window_state(void);

/**
 * @brief 触发后台预加载任务
 * 这个函数由 BLE 命令处理器在接收到页面后调用
 */
void ble_cache_trigger_prefetch(void);

/**
 * @brief 获取需要预加载的页面列表
 * 返回需要向 Android 请求的页码
 * @param[out] pages 页码数组（调用者分配，大小至少 10）
 * @param max_count 最多返回多少个页码
 * @return 实际返回的页码数量
 */
uint16_t ble_cache_get_prefetch_list(uint16_t* pages, uint16_t max_count);

/**
 * @brief 获取接收状态
 * @return 接收状态结构体
 */
ble_rx_state_t ble_cache_get_rx_state(void);

/**
 * @brief 清空所有缓存（初始化或书籍切换时调用）
 */
void ble_cache_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_CACHE_MANAGER_OPTIMIZED_H
