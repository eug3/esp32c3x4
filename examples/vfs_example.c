/**
 * @file vfs_example.c
 * @brief VFS虚拟文件系统使用示例
 * 
 * 演示如何使用VFS统一读取本地和BLE远程文件
 */

#include "vfs/vfs_reader.h"
#include "display_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "VFS_EXAMPLE";

// ========== 示例1: 读取本地TXT文件 ==========

void example_read_local_file(void) {
    ESP_LOGI(TAG, "=== Example 1: Read Local File ===");
    
    // 打开本地文件
    vfs_file_t *file = vfs_open("/sdcard/books/test.txt");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open local file");
        return;
    }
    
    // 读取前2048字节
    char buffer[2048];
    int bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        ESP_LOGI(TAG, "Read %d bytes from local file", bytes);
        ESP_LOGI(TAG, "Content: %s", buffer);
    }
    
    // 关闭文件
    vfs_close(file);
    ESP_LOGI(TAG, "Local file closed");
}

// ========== 示例2: 读取BLE远程文件 ==========

void example_read_ble_file(void) {
    ESP_LOGI(TAG, "=== Example 2: Read BLE Remote File ===");
    
    // 打开BLE虚拟文件
    vfs_file_t *file = vfs_open("ble://my_book");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open BLE file");
        return;
    }
    
    // 设置预加载窗口
    vfs_set_prefetch_window(file, 5);
    ESP_LOGI(TAG, "Prefetch window set to 5 pages");
    
    // 读取第一页
    char buffer[2048];
    int bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        ESP_LOGI(TAG, "Read %d bytes from BLE file", bytes);
        
        // 查看缓存统计
        uint32_t hits, misses;
        if (vfs_get_cache_stats(file, &hits, &misses)) {
            ESP_LOGI(TAG, "Cache stats: %lu hits, %lu misses", hits, misses);
        }
    } else {
        ESP_LOGW(TAG, "No data received (waiting for BLE client)");
    }
    
    vfs_close(file);
}

// ========== 示例3: 统一阅读函数 ==========

/**
 * @brief 通用阅读函数 - 自动识别本地/远程
 */
void read_book(const char *identifier) {
    ESP_LOGI(TAG, "=== Example 3: Universal Reader ===");
    ESP_LOGI(TAG, "Opening: %s", identifier);
    
    // 打开文件 (VFS自动识别类型)
    vfs_file_t *file = vfs_open(identifier);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open: %s", identifier);
        return;
    }
    
    // 如果是BLE文件,启用预加载
    if (file->source == VFS_SOURCE_BLE) {
        vfs_set_prefetch_window(file, 5);
        ESP_LOGI(TAG, "BLE file detected, prefetch enabled");
    }
    
    // 统一的读取逻辑
    char page_buffer[2048];
    int page_num = 1;
    
    while (!vfs_eof(file)) {
        int bytes = vfs_read(file, page_buffer, sizeof(page_buffer) - 1);
        
        if (bytes <= 0) {
            ESP_LOGW(TAG, "No more data or error");
            break;
        }
        
        page_buffer[bytes] = '\0';
        ESP_LOGI(TAG, "=== Page %d (%d bytes) ===", page_num, bytes);
        ESP_LOGI(TAG, "%s", page_buffer);
        
        page_num++;
        
        // 演示用:只读前3页
        if (page_num > 3) {
            break;
        }
        
        // 模拟用户翻页延迟
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 打印统计信息
    if (file->source == VFS_SOURCE_BLE) {
        uint32_t hits, misses;
        if (vfs_get_cache_stats(file, &hits, &misses)) {
            float hit_rate = (hits + misses > 0) 
                ? (float)hits / (hits + misses) * 100.0f 
                : 0.0f;
            ESP_LOGI(TAG, "Final cache hit rate: %.1f%%", hit_rate);
        }
    }
    
    vfs_close(file);
    ESP_LOGI(TAG, "Book closed");
}

// ========== 示例4: 跳转到指定位置 ==========

void example_seek_and_read(void) {
    ESP_LOGI(TAG, "=== Example 4: Seek and Read ===");
    
    vfs_file_t *file = vfs_open("/sdcard/books/test.txt");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file");
        return;
    }
    
    // 获取文件大小
    long file_size = vfs_size(file);
    ESP_LOGI(TAG, "File size: %ld bytes", file_size);
    
    // 跳转到中间位置
    long middle = file_size / 2;
    if (vfs_seek(file, middle, SEEK_SET)) {
        ESP_LOGI(TAG, "Seeked to position %ld", middle);
        
        // 读取
        char buffer[256];
        int bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            ESP_LOGI(TAG, "Content at middle: %s", buffer);
        }
    }
    
    // 跳回开头
    vfs_seek(file, 0, SEEK_SET);
    ESP_LOGI(TAG, "Current position: %ld", vfs_tell(file));
    
    vfs_close(file);
}

// ========== 示例5: 动态切换 ==========

void example_switch_sources(void) {
    ESP_LOGI(TAG, "=== Example 5: Switch Between Sources ===");
    
    // 先读本地文件
    ESP_LOGI(TAG, "--- Reading local file ---");
    read_book("/sdcard/books/local.txt");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 再读BLE远程文件
    ESP_LOGI(TAG, "--- Reading BLE remote file ---");
    read_book("ble://12345678/abcdef00");
    
    ESP_LOGI(TAG, "Switched seamlessly!");
}

// ========== 主示例入口 ==========

void vfs_examples_run_all(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  VFS Virtual File System Examples");
    ESP_LOGI(TAG, "========================================");
    
    // 初始化VFS
    if (!vfs_init()) {
        ESP_LOGE(TAG, "VFS initialization failed");
        return;
    }
    
    // 运行示例
    example_read_local_file();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    example_read_ble_file();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    example_seek_and_read();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    example_switch_sources();
    
    // 清理
    vfs_deinit();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  All examples completed!");
    ESP_LOGI(TAG, "========================================");
}

// ========== 集成到主程序 ==========

/**
 * @brief 在主程序中调用
 */
void app_main(void) {
    // ... 其他初始化 ...
    
    // 运行VFS示例
    vfs_examples_run_all();
    
    // ... 正常程序逻辑 ...
}
