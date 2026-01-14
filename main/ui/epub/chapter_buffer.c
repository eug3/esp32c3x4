/**
 * @file chapter_buffer.c
 * @brief 章节 Flash 缓冲区实现 - 使用 mmap 实现零拷贝访问
 */

#include "chapter_buffer.h"
#include "epub_parser.h"
#include "epub_zip.h"
#include <string.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <spi_flash_mmap.h>
#include <esp_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "CHAPTER_BUF";

// 魔数
#define CHAPTER_BUFFER_MAGIC    0x43485054  // "CHPT"
#define CHAPTER_BUFFER_VERSION  1

// 分区名称
#define CHAPTER_PARTITION_NAME  "chapter_buf"

// 内部状态
static struct {
    bool initialized;
    const esp_partition_t *partition;
    spi_flash_mmap_handle_t mmap_handle;
    const uint8_t *mmap_ptr;            // mmap 后的虚拟地址
    chapter_buffer_meta_t meta;         // 缓存的元数据
    SemaphoreHandle_t mutex;
    uint32_t load_count;                // 统计：加载次数
} s_ctx = {0};

// ============================================================================
// 内部函数
// ============================================================================

static bool read_meta(void)
{
    if (!s_ctx.partition) return false;
    
    // 从 Flash 读取元数据
    esp_err_t err = esp_partition_read(s_ctx.partition, 0, &s_ctx.meta, sizeof(s_ctx.meta));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read meta: %s", esp_err_to_name(err));
        return false;
    }
    
    // 验证魔数
    if (s_ctx.meta.magic != CHAPTER_BUFFER_MAGIC) {
        ESP_LOGW(TAG, "Invalid magic: 0x%08x (expected 0x%08x)", 
                 (unsigned)s_ctx.meta.magic, CHAPTER_BUFFER_MAGIC);
        memset(&s_ctx.meta, 0, sizeof(s_ctx.meta));
        s_ctx.meta.state = CHAPTER_BUF_STATE_EMPTY;
        return false;
    }
    
    return true;
}

static bool write_meta(void)
{
    if (!s_ctx.partition) return false;
    
    s_ctx.meta.magic = CHAPTER_BUFFER_MAGIC;
    s_ctx.meta.version = CHAPTER_BUFFER_VERSION;
    
    // 擦除元数据扇区 (4KB)
    esp_err_t err = esp_partition_erase_range(s_ctx.partition, 0, CHAPTER_BUFFER_META_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase meta sector: %s", esp_err_to_name(err));
        return false;
    }
    
    // 写入元数据
    err = esp_partition_write(s_ctx.partition, 0, &s_ctx.meta, sizeof(s_ctx.meta));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write meta: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

static bool write_content(const uint8_t *data, size_t size)
{
    if (!s_ctx.partition || !data || size == 0) return false;
    if (size > CHAPTER_BUFFER_MAX_CONTENT) {
        ESP_LOGE(TAG, "Content too large: %u > %u", (unsigned)size, CHAPTER_BUFFER_MAX_CONTENT);
        return false;
    }
    
    // 计算需要擦除的扇区数量 (4KB 对齐)
    size_t erase_size = ((size + CHAPTER_BUFFER_META_SIZE + 4095) / 4096) * 4096;
    
    ESP_LOGI(TAG, "Writing %u bytes to Flash (erase %u bytes)", (unsigned)size, (unsigned)erase_size);
    
    // 擦除内容区域 (从元数据之后开始)
    // 注意：我们需要重新擦除包括元数据在内的区域，因为 Flash 写入前必须擦除
    esp_err_t err = esp_partition_erase_range(s_ctx.partition, 0, erase_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase content area: %s", esp_err_to_name(err));
        return false;
    }
    
    // 分块写入内容 (4KB 对齐更高效)
    const size_t WRITE_CHUNK = 4096;
    size_t offset = CHAPTER_BUFFER_META_SIZE;
    size_t remaining = size;
    const uint8_t *ptr = data;
    
    while (remaining > 0) {
        size_t chunk = (remaining > WRITE_CHUNK) ? WRITE_CHUNK : remaining;
        
        err = esp_partition_write(s_ctx.partition, offset, ptr, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write content at offset 0x%x: %s", 
                     (unsigned)offset, esp_err_to_name(err));
            return false;
        }
        
        offset += chunk;
        ptr += chunk;
        remaining -= chunk;
    }
    
    // 更新元数据
    s_ctx.meta.content_size = size;
    s_ctx.meta.content_crc32 = esp_crc32_le(0, data, size);
    s_ctx.meta.state = CHAPTER_BUF_STATE_READY;
    
    if (!write_meta()) {
        return false;
    }
    
    ESP_LOGI(TAG, "Content written successfully: %u bytes, CRC32=0x%08x", 
             (unsigned)size, (unsigned)s_ctx.meta.content_crc32);
    
    return true;
}

// ============================================================================
// 公共 API
// ============================================================================

bool chapter_buffer_init(void)
{
    if (s_ctx.initialized) {
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing chapter buffer (2MB Flash partition)");
    
    // 创建互斥锁
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // 查找分区
    s_ctx.partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x82, CHAPTER_PARTITION_NAME);
    
    if (!s_ctx.partition) {
        ESP_LOGE(TAG, "Partition '%s' not found", CHAPTER_PARTITION_NAME);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return false;
    }
    
    ESP_LOGI(TAG, "Found partition: offset=0x%x, size=%u bytes (%.2f MB)",
             (unsigned)s_ctx.partition->address, 
             (unsigned)s_ctx.partition->size,
             s_ctx.partition->size / 1024.0 / 1024.0);
    
    // mmap 整个分区 (只读)
    esp_err_t err = esp_partition_mmap(
        s_ctx.partition, 0, s_ctx.partition->size,
        ESP_PARTITION_MMAP_DATA, (const void **)&s_ctx.mmap_ptr, &s_ctx.mmap_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return false;
    }
    
    ESP_LOGI(TAG, "Partition mmap'd at %p", s_ctx.mmap_ptr);
    
    // 读取现有元数据
    read_meta();
    
    s_ctx.initialized = true;
    s_ctx.load_count = 0;
    
    if (s_ctx.meta.state == CHAPTER_BUF_STATE_READY) {
        ESP_LOGI(TAG, "Existing cache found: %s chapter %d (%u bytes)",
                 s_ctx.meta.epub_path, (int)s_ctx.meta.chapter_index, 
                 (unsigned)s_ctx.meta.content_size);
    } else {
        ESP_LOGI(TAG, "Chapter buffer ready (empty)");
    }
    
    return true;
}

void chapter_buffer_deinit(void)
{
    if (!s_ctx.initialized) {
        return;
    }
    
    if (s_ctx.mmap_handle) {
        spi_flash_munmap(s_ctx.mmap_handle);
        s_ctx.mmap_handle = 0;
        s_ctx.mmap_ptr = NULL;
    }
    
    if (s_ctx.mutex) {
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
    }
    
    s_ctx.initialized = false;
    ESP_LOGI(TAG, "Chapter buffer deinitialized");
}

chapter_buffer_state_t chapter_buffer_get_state(void)
{
    return (chapter_buffer_state_t)s_ctx.meta.state;
}

bool chapter_buffer_is_cached(const char *epub_path, int chapter_index)
{
    if (!s_ctx.initialized || !epub_path) {
        return false;
    }
    
    if (s_ctx.meta.state != CHAPTER_BUF_STATE_READY) {
        return false;
    }
    
    return (strcmp(s_ctx.meta.epub_path, epub_path) == 0 && 
            s_ctx.meta.chapter_index == (uint32_t)chapter_index);
}

bool chapter_buffer_load(const char *epub_path, int chapter_index, 
                         const char *content_file, const uint8_t *data, size_t size)
{
    if (!s_ctx.initialized || !epub_path || !data || size == 0) {
        return false;
    }
    
    if (size > CHAPTER_BUFFER_MAX_CONTENT) {
        ESP_LOGE(TAG, "Chapter too large: %u > %u", (unsigned)size, CHAPTER_BUFFER_MAX_CONTENT);
        return false;
    }
    
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    
    ESP_LOGI(TAG, "Loading chapter %d from %s (%u bytes)", chapter_index, epub_path, (unsigned)size);
    
    // 更新元数据
    s_ctx.meta.state = CHAPTER_BUF_STATE_LOADING;
    strncpy(s_ctx.meta.epub_path, epub_path, sizeof(s_ctx.meta.epub_path) - 1);
    s_ctx.meta.epub_path[sizeof(s_ctx.meta.epub_path) - 1] = '\0';
    s_ctx.meta.chapter_index = chapter_index;
    if (content_file) {
        strncpy(s_ctx.meta.content_file, content_file, sizeof(s_ctx.meta.content_file) - 1);
        s_ctx.meta.content_file[sizeof(s_ctx.meta.content_file) - 1] = '\0';
    }
    
    bool success = write_content(data, size);
    
    if (success) {
        s_ctx.load_count++;
        ESP_LOGI(TAG, "✓ Chapter %d loaded to Flash buffer", chapter_index);
    } else {
        s_ctx.meta.state = CHAPTER_BUF_STATE_ERROR;
        ESP_LOGE(TAG, "✗ Failed to load chapter %d", chapter_index);
    }
    
    xSemaphoreGive(s_ctx.mutex);
    return success;
}

bool chapter_buffer_load_from_epub(const char *epub_path, int chapter_index)
{
    if (!s_ctx.initialized || !epub_path) {
        return false;
    }
    
    // 检查是否已缓存
    if (chapter_buffer_is_cached(epub_path, chapter_index)) {
        ESP_LOGD(TAG, "Chapter %d already cached", chapter_index);
        return true;
    }
    
    ESP_LOGI(TAG, "Loading chapter %d from EPUB: %s", chapter_index, epub_path);
    
    // 使用临时 reader 获取章节信息
    epub_reader_t temp_reader;
    if (!epub_parser_init(&temp_reader)) {
        ESP_LOGE(TAG, "Failed to init temp reader");
        return false;
    }
    
    if (!epub_parser_open(&temp_reader, epub_path)) {
        ESP_LOGE(TAG, "Failed to open EPUB");
        epub_parser_cleanup(&temp_reader);
        return false;
    }
    
    // 获取章节信息
    const epub_chapter_t *chapter = epub_parser_get_chapter(&temp_reader, chapter_index);
    if (!chapter) {
        ESP_LOGE(TAG, "Failed to get chapter info for index %d", chapter_index);
        epub_parser_close(&temp_reader);
        epub_parser_cleanup(&temp_reader);
        return false;
    }
    
    // 保存章节文件路径（章节关闭后指针失效）
    char content_file[128];
    strncpy(content_file, chapter->content_file, sizeof(content_file) - 1);
    content_file[sizeof(content_file) - 1] = '\0';
    
    epub_parser_close(&temp_reader);
    epub_parser_cleanup(&temp_reader);
    
    // 打开 ZIP 获取文件
    epub_zip_t *zip = epub_zip_open(epub_path);
    if (!zip) {
        ESP_LOGE(TAG, "Failed to open EPUB ZIP");
        return false;
    }
    
    // 查找章节文件
    const epub_zip_file_info_t *file_info = epub_zip_find_file(zip, content_file);
    if (!file_info) {
        ESP_LOGE(TAG, "Chapter file not found: %s", content_file);
        epub_zip_close(zip);
        return false;
    }
    
    // 检查大小
    if (file_info->uncompressed_size > CHAPTER_BUFFER_MAX_CONTENT) {
        ESP_LOGE(TAG, "Chapter too large: %u bytes", (unsigned)file_info->uncompressed_size);
        epub_zip_close(zip);
        return false;
    }
    
    // 分配临时缓冲区解压
    uint8_t *temp_buffer = malloc(file_info->uncompressed_size);
    if (!temp_buffer) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for extraction", 
                 (unsigned)file_info->uncompressed_size);
        epub_zip_close(zip);
        return false;
    }
    
    // 解压
    int extract_size = epub_zip_extract_file(zip, file_info, (char *)temp_buffer, 
                                             file_info->uncompressed_size);
    epub_zip_close(zip);
    
    if (extract_size <= 0) {
        ESP_LOGE(TAG, "Failed to extract chapter: %d", extract_size);
        free(temp_buffer);
        return false;
    }
    
    // 写入 Flash
    bool success = chapter_buffer_load(epub_path, chapter_index, 
                                       content_file, 
                                       temp_buffer, extract_size);
    
    free(temp_buffer);
    return success;
}

const char *chapter_buffer_get_content(size_t *out_size)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return NULL;
    }
    
    if (s_ctx.meta.state != CHAPTER_BUF_STATE_READY) {
        ESP_LOGE(TAG, "Buffer not ready (state=%d)", s_ctx.meta.state);
        return NULL;
    }
    
    if (!s_ctx.mmap_ptr) {
        ESP_LOGE(TAG, "mmap pointer is NULL");
        return NULL;
    }
    
    // 返回内容区域的指针 (跳过元数据)
    const char *content = (const char *)(s_ctx.mmap_ptr + CHAPTER_BUFFER_META_SIZE);
    
    if (out_size) {
        *out_size = s_ctx.meta.content_size;
    }
    
    ESP_LOGD(TAG, "Returning content pointer: %p, size=%u", content, (unsigned)s_ctx.meta.content_size);
    
    return content;
}

bool chapter_buffer_get_info(char *out_epub_path, int *out_chapter_index)
{
    if (!s_ctx.initialized || s_ctx.meta.state != CHAPTER_BUF_STATE_READY) {
        return false;
    }
    
    if (out_epub_path) {
        strcpy(out_epub_path, s_ctx.meta.epub_path);
    }
    if (out_chapter_index) {
        *out_chapter_index = (int)s_ctx.meta.chapter_index;
    }
    
    return true;
}

void chapter_buffer_clear(void)
{
    if (!s_ctx.initialized) {
        return;
    }
    
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    
    memset(&s_ctx.meta, 0, sizeof(s_ctx.meta));
    s_ctx.meta.state = CHAPTER_BUF_STATE_EMPTY;
    write_meta();
    
    ESP_LOGI(TAG, "Chapter buffer cleared");
    
    xSemaphoreGive(s_ctx.mutex);
}

void chapter_buffer_get_stats(size_t *out_total_size, size_t *out_used_size, 
                              uint32_t *out_load_count)
{
    if (out_total_size) {
        *out_total_size = CHAPTER_BUFFER_MAX_CONTENT;
    }
    if (out_used_size) {
        *out_used_size = (s_ctx.meta.state == CHAPTER_BUF_STATE_READY) ? 
                         s_ctx.meta.content_size : 0;
    }
    if (out_load_count) {
        *out_load_count = s_ctx.load_count;
    }
}
