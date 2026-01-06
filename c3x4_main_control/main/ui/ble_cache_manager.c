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
#define BLE_CACHE_DIR "/data/ble_cache"

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

bool ble_cache_save_page(uint16_t book_id, uint16_t page_num, 
                         const uint8_t *data, uint32_t size)
{
    if (!s_cache_state.initialized || data == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for save_page");
        return false;
    }

    char filename[64];
    if (!make_cache_filename(book_id, page_num, filename, sizeof(filename))) {
        return false;
    }

    // 打开文件进行写入
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }

    // 写入数据
    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Failed to write all data to %s (wrote %zu/%u bytes)", 
                 filename, written, size);
        // 删除不完整的文件
        remove(filename);
        return false;
    }

    ESP_LOGI(TAG, "Page saved: book=%04x, page=%u, size=%u", book_id, page_num, size);
    return true;
}

uint32_t ble_cache_load_page(uint16_t book_id, uint16_t page_num,
                             uint8_t *buffer, uint32_t buffer_size)
{
    if (!s_cache_state.initialized || buffer == NULL || buffer_size == 0) {
        return 0;
    }

    char filename[64];
    if (!make_cache_filename(book_id, page_num, filename, sizeof(filename))) {
        return 0;
    }

    // 检查文件是否存在
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        return 0;
    }

    // 读取文件大小
    fseek(f, 0, SEEK_END);
    uint32_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // 确保缓冲区足够大
    uint32_t read_size = (file_size < buffer_size) ? file_size : buffer_size;

    // 读取数据
    size_t read = fread(buffer, 1, read_size, f);
    fclose(f);

    if (read != read_size) {
        ESP_LOGW(TAG, "Failed to read all data from %s", filename);
        return 0;
    }

    ESP_LOGI(TAG, "Page loaded: book=%04x, page=%u, size=%u", book_id, page_num, read_size);
    return read_size;
}

bool ble_cache_page_exists(uint16_t book_id, uint16_t page_num)
{
    if (!s_cache_state.initialized) {
        return false;
    }

    char filename[64];
    if (!make_cache_filename(book_id, page_num, filename, sizeof(filename))) {
        return false;
    }

    struct stat st;
    return (stat(filename, &st) == 0);
}

int32_t ble_cache_get_min_page(uint16_t book_id)
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
            uint32_t page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);
            if (page < min_page) {
                min_page = page;
            }
        }
    }

    closedir(dir);
    return (min_page == INT32_MAX) ? -1 : min_page;
}

int32_t ble_cache_get_max_page(uint16_t book_id)
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
            uint32_t page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);
            if (page > max_page) {
                max_page = page;
            }
        }
    }

    closedir(dir);
    return max_page;
}

uint32_t ble_cache_cleanup_outside_range(uint16_t book_id, 
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
            uint32_t page = 0;
            sscanf(entry->d_name + strlen(prefix), "%u", &page);
            
            // 删除范围外的页面
            if (page < min_page || page > max_page) {
                char filename[64];
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

uint32_t ble_cache_clear_book(uint16_t book_id)
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
            char filename[64];
            snprintf(filename, sizeof(filename), "%s/%s", BLE_CACHE_DIR, entry->d_name);
            if (remove(filename) == 0) {
                deleted_count++;
            }
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Cleared book cache: book=%04x, deleted %u pages", book_id, deleted_count);
    return deleted_count;
}

uint32_t ble_cache_clear_all(void)
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

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".bin") != NULL) {
            char filename[64];
            snprintf(filename, sizeof(filename), "%s/%s", BLE_CACHE_DIR, entry->d_name);
            if (remove(filename) == 0) {
                deleted_count++;
            }
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Cleared all cache, deleted %u pages", deleted_count);
    return deleted_count;
}

bool ble_cache_get_stats(ble_cache_stats_t *stats)
{
    if (!s_cache_state.initialized || stats == NULL) {
        return false;
    }

    // 初始化统计
    stats->total_cached_pages = 0;
    stats->total_size_bytes = 0;
    stats->free_space_bytes = 0;

    // 遍历缓存目录统计
    DIR *dir = opendir(BLE_CACHE_DIR);
    if (dir == NULL) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".bin") != NULL) {
            char filename[64];
            snprintf(filename, sizeof(filename), "%s/%s", BLE_CACHE_DIR, entry->d_name);
            
            struct stat st;
            if (stat(filename, &st) == 0) {
                stats->total_cached_pages++;
                stats->total_size_bytes += st.st_size;
            }
        }
    }
    closedir(dir);

    // TODO: 获取 LittleFS 剩余空间
    // 需要使用 esp_littlefs_info() 或其他方法

    ESP_LOGI(TAG, "Cache stats: pages=%u, size=%u bytes", 
             stats->total_cached_pages, stats->total_size_bytes);
    return true;
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
