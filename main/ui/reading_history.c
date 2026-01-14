/**
 * @file reading_history.c
 * @brief 阅读历史管理器实现
 */

#include "reading_history.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <time.h>

static const char *TAG = "READING_HISTORY";

// NVS 配置
#define NVS_NAMESPACE      "book_history"
#define NVS_KEY_COUNT      "count"        // 记录数量
#define NVS_KEY_BOOK_FMT   "book_%d"      // 书籍记录：book_0, book_1, ...

// 内部缓存（避免频繁读写 NVS）
static reading_history_t s_cached_history = {0};
static bool s_cache_valid = false;

// 辅助函数：计算字符串哈希（用于比较文件路径）
static uint32_t hash_string(const char *str)
{
    if (!str) return 0;
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// 辅助函数：从 NVS 加载单个书籍记录
static bool load_record_from_nvs(nvs_handle_t handle, int index, book_record_t *record)
{
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_BOOK_FMT, index);
    
    size_t required_size = sizeof(book_record_t);
    esp_err_t err = nvs_get_blob(handle, key, record, &required_size);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load record %d: %s", index, esp_err_to_name(err));
        return false;
    }
    
    return true;
}

// 辅助函数：保存单个书籍记录到 NVS
static bool save_record_to_nvs(nvs_handle_t handle, int index, const book_record_t *record)
{
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_BOOK_FMT, index);
    
    esp_err_t err = nvs_set_blob(handle, key, record, sizeof(book_record_t));
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save record %d: %s", index, esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool reading_history_init(void)
{
    ESP_LOGI(TAG, "Initializing reading history manager");
    
    // 尝试加载缓存
    if (reading_history_load_all(&s_cached_history)) {
        s_cache_valid = true;
        ESP_LOGI(TAG, "Loaded %d book records from NVS", s_cached_history.count);
        return true;
    }
    
    // 没有历史记录，初始化为空
    memset(&s_cached_history, 0, sizeof(s_cached_history));
    s_cache_valid = true;
    ESP_LOGI(TAG, "No previous reading history, starting fresh");
    
    return true;
}

bool reading_history_load_all(reading_history_t *history)
{
    if (!history) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s (no history yet?)", esp_err_to_name(err));
        memset(history, 0, sizeof(reading_history_t));
        return false;
    }
    
    // 读取记录数量
    int32_t count = 0;
    err = nvs_get_i32(handle, NVS_KEY_COUNT, &count);
    if (err != ESP_OK || count < 0 || count > READING_HISTORY_MAX_BOOKS) {
        ESP_LOGW(TAG, "Invalid count in NVS: %ld", count);
        nvs_close(handle);
        memset(history, 0, sizeof(reading_history_t));
        return false;
    }
    
    // 读取所有记录
    history->count = 0;
    for (int i = 0; i < count; i++) {
        if (load_record_from_nvs(handle, i, &history->books[i])) {
            if (history->books[i].is_valid) {
                history->count++;
            }
        }
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Loaded %d/%ld valid records", history->count, count);
    return true;
}

bool reading_history_save_record(const book_record_t *record)
{
    if (!record || !record->is_valid) {
        return false;
    }
    
    // 更新缓存
    if (!s_cache_valid) {
        reading_history_load_all(&s_cached_history);
        s_cache_valid = true;
    }
    
    // 查找是否已存在
    int existing_index = -1;
    for (int i = 0; i < s_cached_history.count; i++) {
        if (strcmp(s_cached_history.books[i].file_path, record->file_path) == 0) {
            existing_index = i;
            break;
        }
    }
    
    if (existing_index >= 0) {
        // 更新现有记录并移到最前
        book_record_t updated = *record;
        updated.last_read_time = time(NULL);
        
        // 移动到最前
        for (int i = existing_index; i > 0; i--) {
            s_cached_history.books[i] = s_cached_history.books[i - 1];
        }
        s_cached_history.books[0] = updated;
        
        ESP_LOGI(TAG, "Updated existing record: %s", record->title);
    } else {
        // 新记录
        book_record_t new_record = *record;
        new_record.last_read_time = time(NULL);
        
        // 如果列表已满，删除最后一个
        if (s_cached_history.count >= READING_HISTORY_MAX_BOOKS) {
            s_cached_history.count = READING_HISTORY_MAX_BOOKS - 1;
            ESP_LOGI(TAG, "History full, removing oldest record");
        }
        
        // 所有记录后移
        for (int i = s_cached_history.count; i > 0; i--) {
            s_cached_history.books[i] = s_cached_history.books[i - 1];
        }
        
        s_cached_history.books[0] = new_record;
        s_cached_history.count++;
        
        ESP_LOGI(TAG, "Added new record: %s (%d total)", record->title, s_cached_history.count);
    }
    
    // 保存到 NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return false;
    }
    
    // 保存记录数量
    err = nvs_set_i32(handle, NVS_KEY_COUNT, s_cached_history.count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    // 保存所有记录
    bool success = true;
    for (int i = 0; i < s_cached_history.count; i++) {
        if (!save_record_to_nvs(handle, i, &s_cached_history.books[i])) {
            success = false;
            break;
        }
    }
    
    if (success) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    nvs_close(handle);
    
    return success;
}

bool reading_history_load_record(const char *file_path, book_record_t *record)
{
    if (!file_path || !record) {
        return false;
    }
    
    if (!s_cache_valid) {
        reading_history_load_all(&s_cached_history);
        s_cache_valid = true;
    }
    
    for (int i = 0; i < s_cached_history.count; i++) {
        if (strcmp(s_cached_history.books[i].file_path, file_path) == 0) {
            *record = s_cached_history.books[i];
            ESP_LOGI(TAG, "Found record: %s (chapter=%ld, page=%ld)", 
                     record->title, record->position.chapter, record->position.page);
            return true;
        }
    }
    
    ESP_LOGW(TAG, "No record found for: %s", file_path);
    return false;
}

bool reading_history_update_position(const char *file_path, const reading_position_t *position)
{
    if (!file_path || !position) {
        return false;
    }
    
    book_record_t record;
    if (!reading_history_load_record(file_path, &record)) {
        ESP_LOGW(TAG, "Cannot update position for unknown book: %s", file_path);
        return false;
    }
    
    record.position = *position;
    record.last_read_time = time(NULL);
    
    return reading_history_save_record(&record);
}

bool reading_history_delete_record(const char *file_path)
{
    if (!file_path) {
        return false;
    }
    
    if (!s_cache_valid) {
        reading_history_load_all(&s_cached_history);
        s_cache_valid = true;
    }
    
    // 查找并删除
    int found_index = -1;
    for (int i = 0; i < s_cached_history.count; i++) {
        if (strcmp(s_cached_history.books[i].file_path, file_path) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index < 0) {
        ESP_LOGW(TAG, "Record not found for deletion: %s", file_path);
        return false;
    }
    
    // 移动后续记录
    for (int i = found_index; i < s_cached_history.count - 1; i++) {
        s_cached_history.books[i] = s_cached_history.books[i + 1];
    }
    s_cached_history.count--;
    
    // 清空最后一个位置
    memset(&s_cached_history.books[s_cached_history.count], 0, sizeof(book_record_t));
    
    ESP_LOGI(TAG, "Deleted record: %s", file_path);
    
    // 保存到 NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    
    nvs_set_i32(handle, NVS_KEY_COUNT, s_cached_history.count);
    
    for (int i = 0; i < s_cached_history.count; i++) {
        save_record_to_nvs(handle, i, &s_cached_history.books[i]);
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    
    return true;
}

bool reading_history_clear_all(void)
{
    ESP_LOGI(TAG, "Clearing all reading history");
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    
    err = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    
    memset(&s_cached_history, 0, sizeof(s_cached_history));
    s_cache_valid = true;
    
    return (err == ESP_OK);
}

int reading_history_get_recent_books(int count, book_record_t *records)
{
    if (!records || count <= 0) {
        return 0;
    }
    
    if (!s_cache_valid) {
        reading_history_load_all(&s_cached_history);
        s_cache_valid = true;
    }
    
    int return_count = (count < s_cached_history.count) ? count : s_cached_history.count;
    
    for (int i = 0; i < return_count; i++) {
        records[i] = s_cached_history.books[i];
    }
    
    return return_count;
}

const char* reading_history_get_last_book_path(void)
{
    if (!s_cache_valid) {
        reading_history_load_all(&s_cached_history);
        s_cache_valid = true;
    }
    
    if (s_cached_history.count > 0 && s_cached_history.books[0].is_valid) {
        return s_cached_history.books[0].file_path;
    }
    
    return NULL;
}

bool reading_history_mark_as_read(const char *file_path, uint32_t read_duration)
{
    if (!file_path) {
        return false;
    }
    
    book_record_t record;
    if (!reading_history_load_record(file_path, &record)) {
        return false;
    }
    
    record.last_read_time = time(NULL);
    record.total_read_time += read_duration;
    
    return reading_history_save_record(&record);
}

book_record_t reading_history_create_record(const char *file_path, const char *title, book_type_t type)
{
    book_record_t record;
    memset(&record, 0, sizeof(record));
    
    if (file_path) {
        strncpy(record.file_path, file_path, READING_HISTORY_MAX_PATH_LEN - 1);
    }
    
    if (title) {
        strncpy(record.title, title, READING_HISTORY_MAX_TITLE_LEN - 1);
    } else if (file_path) {
        reading_history_extract_title(file_path, record.title, READING_HISTORY_MAX_TITLE_LEN);
    }
    
    record.type = type;
    record.is_valid = true;
    record.last_read_time = time(NULL);
    
    return record;
}

void reading_history_extract_title(const char *file_path, char *title, size_t title_size)
{
    if (!file_path || !title || title_size == 0) {
        return;
    }
    
    // 查找最后一个 '/'
    const char *filename = strrchr(file_path, '/');
    if (filename) {
        filename++; // 跳过 '/'
    } else {
        filename = file_path;
    }
    
    // 复制文件名
    strncpy(title, filename, title_size - 1);
    title[title_size - 1] = '\0';
    
    // 去除扩展名
    char *ext = strrchr(title, '.');
    if (ext) {
        *ext = '\0';
    }
}

const char* reading_history_get_type_string(book_type_t type)
{
    switch (type) {
        case BOOK_TYPE_TXT:  return "TXT";
        case BOOK_TYPE_EPUB: return "EPUB";
        default:             return "Unknown";
    }
}

void reading_history_format_time(time_t timestamp, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    if (timestamp == 0) {
        snprintf(buffer, buffer_size, "Never");
        return;
    }
    
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M", &timeinfo);
}
