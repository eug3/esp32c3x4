/**
 * @file ble_write_queue.c
 * @brief BLE 文件写入队列管理器实现
 * @note 
 * - 蓝牙回调只负责收包进队列（非阻塞）
 * - 低优先级后台任务负责写入 Flash
 * - 4KB 对齐写入减少 Flash 磨损
 */

#include "ble_write_queue.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

static const char *TAG = "BLE_WRITE_Q";

// 写入任务配置
#define WRITE_TASK_STACK_SIZE   4096
#define WRITE_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)  // 低优先级

// 状态
static struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    SemaphoreHandle_t mutex;
    
    FILE *file;
    char filepath[256];
    uint32_t total_size;
    uint32_t bytes_written;
    
    // 4KB 对齐缓冲区
    uint8_t align_buffer[BLE_WRITE_BLOCK_SIZE];
    size_t align_buffer_len;
    
    bool initialized;
    bool transferring;
    bool cancel_requested;
} s_state = {0};

// 前向声明
static void write_task(void *param);
static bool flush_align_buffer(void);

bool ble_write_queue_init(void)
{
    if (s_state.initialized) {
        return true;
    }
    
    // 创建队列
    s_state.queue = xQueueCreate(BLE_WRITE_QUEUE_DEPTH, sizeof(ble_write_block_t));
    if (s_state.queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }
    
    // 创建互斥锁
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vQueueDelete(s_state.queue);
        return false;
    }
    
    // 创建后台写入任务
    BaseType_t ret = xTaskCreate(write_task, "ble_write", WRITE_TASK_STACK_SIZE,
                                  NULL, WRITE_TASK_PRIORITY, &s_state.task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create write task");
        vSemaphoreDelete(s_state.mutex);
        vQueueDelete(s_state.queue);
        return false;
    }
    
    s_state.initialized = true;
    ESP_LOGI(TAG, "Write queue initialized (block=%d, depth=%d)", 
             BLE_WRITE_BLOCK_SIZE, BLE_WRITE_QUEUE_DEPTH);
    return true;
}

void ble_write_queue_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    // 取消当前传输
    ble_write_queue_cancel();
    
    // 删除任务
    if (s_state.task != NULL) {
        vTaskDelete(s_state.task);
        s_state.task = NULL;
    }
    
    // 删除队列和互斥锁
    if (s_state.queue != NULL) {
        vQueueDelete(s_state.queue);
        s_state.queue = NULL;
    }
    
    if (s_state.mutex != NULL) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }
    
    s_state.initialized = false;
    ESP_LOGI(TAG, "Write queue deinitialized");
}

bool ble_write_queue_start_file(const char *filepath, uint32_t total_size)
{
    if (!s_state.initialized || filepath == NULL) {
        return false;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    if (s_state.transferring) {
        ESP_LOGW(TAG, "Already transferring, cancel first");
        xSemaphoreGive(s_state.mutex);
        return false;
    }
    
    // 打开文件
    s_state.file = fopen(filepath, "wb");
    if (s_state.file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s (errno=%d)", filepath, errno);
        xSemaphoreGive(s_state.mutex);
        return false;
    }
    
    strncpy(s_state.filepath, filepath, sizeof(s_state.filepath) - 1);
    s_state.total_size = total_size;
    s_state.bytes_written = 0;
    s_state.align_buffer_len = 0;
    s_state.transferring = true;
    s_state.cancel_requested = false;
    
    ESP_LOGI(TAG, "Started file transfer: %s (%" PRIu32 " bytes)", filepath, total_size);
    
    xSemaphoreGive(s_state.mutex);
    return true;
}

bool ble_write_queue_push(const uint8_t *data, size_t length)
{
    if (!s_state.initialized || !s_state.transferring || data == NULL || length == 0) {
        return false;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    if (s_state.cancel_requested) {
        xSemaphoreGive(s_state.mutex);
        return false;
    }
    
    const uint8_t *p = data;
    size_t remaining = length;
    
    while (remaining > 0) {
        // 填充对齐缓冲区
        size_t space = BLE_WRITE_BLOCK_SIZE - s_state.align_buffer_len;
        size_t copy = (remaining < space) ? remaining : space;
        
        memcpy(s_state.align_buffer + s_state.align_buffer_len, p, copy);
        s_state.align_buffer_len += copy;
        p += copy;
        remaining -= copy;
        
        // 缓冲区满，发送到队列
        if (s_state.align_buffer_len >= BLE_WRITE_BLOCK_SIZE) {
            ble_write_block_t block;
            memcpy(block.data, s_state.align_buffer, BLE_WRITE_BLOCK_SIZE);
            block.length = BLE_WRITE_BLOCK_SIZE;
            block.file_offset = s_state.bytes_written;
            block.is_last_block = false;
            
            // 非阻塞发送，如果队列满则等待一小段时间
            xSemaphoreGive(s_state.mutex);
            
            if (xQueueSend(s_state.queue, &block, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Queue full, waiting...");
                // 等待队列有空间
                vTaskDelay(pdMS_TO_TICKS(50));
                if (xQueueSend(s_state.queue, &block, pdMS_TO_TICKS(500)) != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to push to queue");
                    return false;
                }
            }
            
            xSemaphoreTake(s_state.mutex, portMAX_DELAY);
            s_state.align_buffer_len = 0;
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return true;
}

static bool flush_align_buffer(void)
{
    if (s_state.align_buffer_len == 0) {
        return true;
    }
    
    ble_write_block_t block;
    memcpy(block.data, s_state.align_buffer, s_state.align_buffer_len);
    block.length = s_state.align_buffer_len;
    block.file_offset = s_state.bytes_written;
    block.is_last_block = true;
    
    s_state.align_buffer_len = 0;
    
    // 发送到队列
    if (xQueueSend(s_state.queue, &block, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to flush final block");
        return false;
    }
    
    return true;
}

bool ble_write_queue_finish(void)
{
    if (!s_state.initialized || !s_state.transferring) {
        return false;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    // 刷新剩余数据
    bool ok = flush_align_buffer();
    
    xSemaphoreGive(s_state.mutex);
    
    // 等待写入任务完成
    int wait_count = 0;
    while (uxQueueMessagesWaiting(s_state.queue) > 0 && wait_count < 100) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_count++;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    // 关闭文件
    if (s_state.file != NULL) {
        fclose(s_state.file);
        s_state.file = NULL;
    }
    
    s_state.transferring = false;
    
    ESP_LOGI(TAG, "File transfer complete: %s (%" PRIu32 " bytes)", 
             s_state.filepath, s_state.bytes_written);
    
    xSemaphoreGive(s_state.mutex);
    return ok;
}

void ble_write_queue_cancel(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    s_state.cancel_requested = true;
    
    // 清空队列
    xQueueReset(s_state.queue);
    
    // 关闭文件
    if (s_state.file != NULL) {
        fclose(s_state.file);
        s_state.file = NULL;
        // 删除不完整的文件
        remove(s_state.filepath);
        ESP_LOGW(TAG, "Transfer cancelled, deleted incomplete file: %s", s_state.filepath);
    }
    
    s_state.transferring = false;
    s_state.align_buffer_len = 0;
    
    xSemaphoreGive(s_state.mutex);
}

bool ble_write_queue_get_progress(uint32_t *bytes_written, uint32_t *total_bytes)
{
    if (!s_state.initialized) {
        return false;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    if (bytes_written) *bytes_written = s_state.bytes_written;
    if (total_bytes) *total_bytes = s_state.total_size;
    bool transferring = s_state.transferring;
    
    xSemaphoreGive(s_state.mutex);
    return transferring;
}

bool ble_write_queue_is_busy(void)
{
    if (!s_state.initialized) {
        return false;
    }
    return s_state.transferring;
}

/**
 * @brief 后台写入任务
 */
static void write_task(void *param)
{
    (void)param;
    ble_write_block_t block;
    
    ESP_LOGI(TAG, "Write task started");
    
    while (1) {
        // 等待数据块
        if (xQueueReceive(s_state.queue, &block, pdMS_TO_TICKS(100)) == pdTRUE) {
            xSemaphoreTake(s_state.mutex, portMAX_DELAY);
            
            if (s_state.file != NULL && !s_state.cancel_requested) {
                // 写入文件
                size_t written = fwrite(block.data, 1, block.length, s_state.file);
                if (written == block.length) {
                    s_state.bytes_written += written;
                    
                    // 每写入一个块就 fsync（减少数据丢失风险）
                    if (block.is_last_block || s_state.bytes_written % (BLE_WRITE_BLOCK_SIZE * 4) == 0) {
                        fflush(s_state.file);
                    }
                    
                    ESP_LOGD(TAG, "Written %" PRIu32 "/%" PRIu32 " bytes", 
                             s_state.bytes_written, s_state.total_size);
                } else {
                    ESP_LOGE(TAG, "Write error: expected %zu, wrote %zu", block.length, written);
                }
            }
            
            xSemaphoreGive(s_state.mutex);
        }
    }
}
