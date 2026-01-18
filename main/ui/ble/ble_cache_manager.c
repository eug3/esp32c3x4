/**
 * @file ble_cache_manager.c
 * @brief 蓝牙页面缓存管理器实现
 */

#include "ble_cache_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "BLE_CACHE";

// 缓存目录路径
#define BLE_CACHE_DIR "/littlefs/ble_pages"

// 缓存预加载参数
#define BLE_PRELOAD_THRESHOLD 2  // 剩余页数少于此值时触发预加载
#define BLE_PRELOAD_COUNT 5      // 每次预加载请求的页数

// 缓存状态
static struct {
    bool initialized;
    ble_cache_preload_cb preload_cb;
} s_cache_state = {
    .initialized = false,
    .preload_cb = NULL,
};

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 生成缓存文件名
 * 格式: /littlefs/ble_cache/book_NNNN_page_MMMMM.bin
 */
static bool make_cache_filename(uint16_t book_id, uint16_t page_num, 
                                char *filename, size_t max_len)
{
    if (filename == NULL || max_len < 64) {
        return false;
    }
    snprintf(filename, max_len, "%s/book_%04x_page_%05u.bin", 
             BLE_CACHE_DIR, book_id, page_num);
    return true;
}

/**
 * @brief 创建缓存目录
 */
static bool ensure_cache_dir_exists(void)
{
    DIR *dir = opendir(BLE_CACHE_DIR);
    if (dir == NULL) {
        // 目录不存在，创建它
        if (mkdir(BLE_CACHE_DIR, 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create cache directory");
            return false;
        }
        ESP_LOGI(TAG, "Cache directory created: %s", BLE_CACHE_DIR);
    } else {
        closedir(dir);
    }
    return true;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool ble_cache_init(void)
{
    if (s_cache_state.initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE cache manager...");

    // 检查LittleFS是否已挂载
    struct stat st;
    if (stat(BLE_CACHE_DIR, &st) != 0) {
        // LittleFS 可能未挂载，这里假设由主程序负责挂载
        ESP_LOGI(TAG, "Cache directory not accessible, will try to create");
    }

    // 确保缓存目录存在
    if (!ensure_cache_dir_exists()) {
        ESP_LOGE(TAG, "Failed to ensure cache directory exists");
        return false;
    }

    s_cache_state.initialized = true;
    ESP_LOGI(TAG, "BLE cache manager initialized");
    return true;
}

void ble_cache_deinit(void)
{
    if (!s_cache_state.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing BLE cache manager");
    s_cache_state.initialized = false;
}

/**
 * @brief 获取已缓存的最小页码
 * @param book_id 书籍ID
 * @return 最小页码，-1 表示没有缓存
 */
static int32_t ble_cache_get_min_page(uint16_t book_id)
{
    if (!s_cache_state.initialized) {
        return -1;
    }

    DIR *dir = opendir(BLE_CACHE_DIR);
    if (dir == NULL) {
        return -1;
    }

    int32_t min_page = INT32_MAX;
    struct dirent *entry;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "book_%04x_page_", book_id);

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            unsigned int page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);
            if (page < min_page) {
                min_page = page;
            }
        }
    }

    closedir(dir);
    return (min_page == INT32_MAX) ? -1 : min_page;
}

/**
 * @brief 获取已缓存的最大页码
 * @param book_id 书籍ID
 * @return 最大页码，-1 表示没有缓存
 */
static int32_t ble_cache_get_max_page(uint16_t book_id)
{
    if (!s_cache_state.initialized) {
        return -1;
    }

    DIR *dir = opendir(BLE_CACHE_DIR);
    if (dir == NULL) {
        return -1;
    }

    int32_t max_page = -1;
    struct dirent *entry;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "book_%04x_page_", book_id);

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            unsigned int page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);
            if (page > max_page) {
                max_page = page;
            }
        }
    }

    closedir(dir);
    return max_page;
}

/**
 * @brief 清理指定页码范围之外的缓存
 * 用于滑动窗口的清理，保留指定范围内的页面
 * @param book_id 书籍ID
 * @param min_page 最小保留页码
 * @param max_page 最大保留页码
 * @return 删除的页面数
 */
static uint32_t ble_cache_cleanup_outside_range(uint16_t book_id,
                                                uint16_t min_page,
                                                uint16_t max_page)
{
    if (!s_cache_state.initialized) {
        return 0;
    }

    DIR *dir = opendir(BLE_CACHE_DIR);
    if (dir == NULL) {
        return 0;
    }

    uint32_t deleted_count = 0;
    struct dirent *entry;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "book_%04x_page_", book_id);

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            unsigned int page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);

            // 删除范围外的页面
            if (page < min_page || page > max_page) {
                char filename[320];
                snprintf(filename, sizeof(filename), "%s/%s", BLE_CACHE_DIR, entry->d_name);
                if (remove(filename) == 0) {
                    deleted_count++;
                    ESP_LOGI(TAG, "Cleaned page: book=%04x, page=%u", book_id, page);
                }
            }
        }
    }

    closedir(dir);
    return deleted_count;
}

void ble_cache_register_preload_cb(ble_cache_preload_cb cb)
{
    s_cache_state.preload_cb = cb;
}

bool ble_cache_update_read_position(uint16_t book_id, uint16_t current_page)
{
    if (!s_cache_state.initialized) {
        return false;
    }

    // 获取当前缓存范围
    int32_t min_page = ble_cache_get_min_page(book_id);
    int32_t max_page = ble_cache_get_max_page(book_id);

    if (min_page < 0 || max_page < 0) {
        return false;  // 没有缓存的页面
    }

    // 检查是否需要预加载（当接近末尾时）
    uint16_t remaining = max_page - current_page;
    if (remaining <= BLE_PRELOAD_THRESHOLD && s_cache_state.preload_cb != NULL) {
        uint16_t start_page = max_page + 1;
        ESP_LOGI(TAG, "Triggering preload: book=%04x, start_page=%u, count=%u",
                 book_id, start_page, BLE_PRELOAD_COUNT);
        s_cache_state.preload_cb(book_id, start_page, BLE_PRELOAD_COUNT);
        return true;
    }

    // 清理窗口外的页面（保留当前页前后各5页）
    uint16_t keep_min = (current_page > 5) ? (current_page - 5) : 0;
    uint16_t keep_max = current_page + 5;
    ble_cache_cleanup_outside_range(book_id, keep_min, keep_max);

    return false;
}
