/**
 * @file vfs_ble_impl.c
 * @brief BLE虚拟文件实现 - 智能缓存+预加载
 */

#include "vfs_reader.h"
#include "ble_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>

static const char *TAG = "VFS_BLE";

#define BLE_VFS_CACHE_DIR "/littlefs/ble_vfs"

// 配置参数
#define BLE_PAGE_SIZE          1024      // 每页大小(字节)

/**
 * @brief BLE文件流式读取管理器（无Cache）
 */
typedef struct {
    // 基本信息
    uint32_t book_hash;
    uint32_t chapter_hash;
    
    // 章节管理
    int current_chapter;         // 当前章节索引
    int total_chapters;          // 总章节数
    
    // 同步机制
    SemaphoreHandle_t mutex;
    
    // 统计
    uint32_t total_reads;
    
    // 状态
    bool initialized;
} ble_file_cache_t;

// ========== 内部辅助函数 ==========

/**
 * @brief 请求BLE客户端发送整个章节文本
 */
static void request_chapter_from_client(ble_file_cache_t *cache) {
    // 构造请求消息: "PAGE:book:chapter" 例: "PAGE:0x12345678:5"
    char request[128];
    snprintf(request, sizeof(request), "PAGE:0x%08lX:%d", 
             (unsigned long)cache->book_hash, cache->current_chapter);
    
    // 通过BLE通知发送章节请求
    ble_manager_send_notification((const uint8_t*)request, strlen(request));
    
    ESP_LOGI(TAG, "Requested chapter %d (book 0x%08lX) from BLE client", 
             cache->current_chapter, (unsigned long)cache->book_hash);
}

/**
 * @brief 获取当前章节文件路径
 * 简化方案：使用固定文件名，避免哈希不一致问题
 */
static void get_chapter_file_path(ble_file_cache_t *cache, char *path, size_t path_len) {
    // 简化：直接使用 chapter 索引，不依赖 book_hash
    snprintf(path, path_len, "%s/current_ch%d.txt", 
             BLE_VFS_CACHE_DIR,
             cache->current_chapter);
    ESP_LOGI(TAG, "get_chapter_file_path: %s", path);
}

// ========== 公共接口 ==========

/**
 * @brief 创建BLE文件流式读取管理器
 */
ble_file_cache_t* vfs_ble_create_cache(uint32_t book_hash, uint32_t chapter_hash) {
    ble_file_cache_t *cache = (ble_file_cache_t*)calloc(1, sizeof(ble_file_cache_t));
    if (!cache) {
        ESP_LOGE(TAG, "Failed to allocate cache");
        return NULL;
    }
    
    cache->book_hash = book_hash;
    cache->chapter_hash = chapter_hash;
    cache->current_chapter = 0;  // 默认从第0章开始
    cache->total_chapters = 1;   // 默认1章，后续可更新
    
    // 创建互斥锁
    cache->mutex = xSemaphoreCreateMutex();
    if (!cache->mutex) {
        free(cache);
        return NULL;
    }
    
    cache->initialized = true;
    
    ESP_LOGI(TAG, "BLE stream reader created: book=0x%08lx", cache->book_hash);
    
    return cache;
}

/**
 * @brief 销毁BLE文件管理器
 */
void vfs_ble_destroy_cache(ble_file_cache_t *cache) {
    if (!cache) return;
    
    if (cache->mutex) {
        vSemaphoreDelete(cache->mutex);
    }
    
    free(cache);
    ESP_LOGI(TAG, "BLE stream reader destroyed");
}

/**
 * @brief BLE文件流式读取（直接从Flash读取，无内存Cache）
 */
int vfs_ble_read(ble_file_cache_t *cache, long position, void *buffer, size_t size) {
    if (!cache || !buffer || size == 0) {
        return -1;
    }
    
    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    cache->total_reads++;
    
    // 获取当前章节文件路径
    char path[128];
    get_chapter_file_path(cache, path, sizeof(path));
    
    ESP_LOGI(TAG, "vfs_ble_read: book_hash=0x%08lX, chapter=%d, path=%s", 
             (unsigned long)cache->book_hash, cache->current_chapter, path);
    
    // 打开章节文件
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Chapter file not found: %s", path);
        xSemaphoreGive(cache->mutex);
        return -1;
    }
    
    // 定位到读取位置
    if (fseek(fp, position, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "fseek failed: position=%ld", position);
        fclose(fp);
        xSemaphoreGive(cache->mutex);
        return -1;
    }
    
    // 流式读取
    size_t bytes_read = fread(buffer, 1, size, fp);
    fclose(fp);
    
    xSemaphoreGive(cache->mutex);
    
    ESP_LOGI(TAG, "Stream read: pos=%ld size=%zu read=%zu", 
             position, size, bytes_read);
    
    return (int)bytes_read;
}

/**
 * @brief 切换到指定章节
 */
bool vfs_ble_switch_chapter(ble_file_cache_t *cache, int chapter_index) {
    if (!cache || chapter_index < 0 || chapter_index >= cache->total_chapters) {
        return false;
    }
    
    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    
    // 切换章节
    cache->current_chapter = chapter_index;
    
    xSemaphoreGive(cache->mutex);
    
    ESP_LOGI(TAG, "Switched to chapter %d", chapter_index);
    
    // 请求新章节的完整文本（一次性下载）
    request_chapter_from_client(cache);
    
    return true;
}

/**
 * @brief 获取当前章节索引
 */
int vfs_ble_get_current_chapter(ble_file_cache_t *cache) {
    if (!cache) return -1;
    
    int chapter;
    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    chapter = cache->current_chapter;
    xSemaphoreGive(cache->mutex);
    
    return chapter;
}

/**
 * @brief 获取总章节数
 */
int vfs_ble_get_total_chapters(ble_file_cache_t *cache) {
    if (!cache) return -1;
    
    int total;
    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    total = cache->total_chapters;
    xSemaphoreGive(cache->mutex);
    
    return total;
}

/**
 * @brief 设置总章节数
 */
bool vfs_ble_set_total_chapters(ble_file_cache_t *cache, int total_chapters) {
    if (!cache || total_chapters < 1) {
        return false;
    }
    
    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    cache->total_chapters = total_chapters;
    xSemaphoreGive(cache->mutex);
    
    ESP_LOGI(TAG, "Total chapters set to %d", total_chapters);
    return true;
}
