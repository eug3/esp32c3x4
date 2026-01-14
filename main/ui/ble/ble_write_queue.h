/**
 * @file ble_write_queue.h
 * @brief BLE 文件写入队列管理器
 * @note 遵循最佳实践：蓝牙回调只负责收包进队列，低优先级任务负责写入 Flash
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 写入块大小（4KB 对齐，减少 Flash 磨损）
#define BLE_WRITE_BLOCK_SIZE    4096

// 队列深度（缓存多少个块）
#define BLE_WRITE_QUEUE_DEPTH   4

/**
 * @brief 写入数据块结构
 */
typedef struct {
    uint8_t data[BLE_WRITE_BLOCK_SIZE];
    size_t length;
    size_t file_offset;     // 在文件中的偏移
    bool is_last_block;     // 是否是最后一个块
} ble_write_block_t;

/**
 * @brief 初始化写入队列和后台写入任务
 * @return true 成功，false 失败
 */
bool ble_write_queue_init(void);

/**
 * @brief 反初始化写入队列
 */
void ble_write_queue_deinit(void);

/**
 * @brief 开始新文件传输
 * @param filepath 目标文件路径
 * @param total_size 文件总大小
 * @return true 成功，false 失败
 */
bool ble_write_queue_start_file(const char *filepath, uint32_t total_size);

/**
 * @brief 将数据块加入写入队列（在 BLE 回调中调用）
 * @param data 数据指针
 * @param length 数据长度
 * @return true 成功加入队列，false 队列满或出错
 * @note 此函数会自动进行 4KB 对齐缓冲
 */
bool ble_write_queue_push(const uint8_t *data, size_t length);

/**
 * @brief 完成文件传输（刷新剩余数据）
 * @return true 成功，false 失败
 */
bool ble_write_queue_finish(void);

/**
 * @brief 取消当前传输
 */
void ble_write_queue_cancel(void);

/**
 * @brief 获取写入进度
 * @param bytes_written 输出：已写入字节数
 * @param total_bytes 输出：总字节数
 * @return true 正在传输，false 空闲
 */
bool ble_write_queue_get_progress(uint32_t *bytes_written, uint32_t *total_bytes);

/**
 * @brief 检查是否正在传输
 */
bool ble_write_queue_is_busy(void);

#ifdef __cplusplus
}
#endif
