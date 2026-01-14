/**
 * @file font_partition.c
 * @brief 字体分区管理实现 - 使用 mmap 零拷贝读取
 */

#include "font_partition.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "FONT_PART";

// 字体分区句柄
static const esp_partition_t *s_font_partition = NULL;

// mmap 句柄和映射地址
static spi_flash_mmap_handle_t s_font_mmap_handle = 0;
static const uint8_t *s_font_mmap_ptr = NULL;

// 字体文件信息（假设 19x25 字体，65536 字符）
#define FONT_TOTAL_CHARS 0x10000u
#define FONT_GLYPH_SIZE 75u  // 19x25: width_byte=3, height=25, 3*25=75

// ESP32-C3 mmap 限制：最大约 4MB
// 实际字体大小：65536 * 75 = 4,915,200 bytes ≈ 4.69MB
// 但分区是 5MB，我们只 mmap 实际需要的大小
#define FONT_MMAP_SIZE (FONT_TOTAL_CHARS * FONT_GLYPH_SIZE)  // 4.69MB

bool font_partition_init(void)
{
    // 查找字体分区
    s_font_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 
                                                 ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
                                                 "font_data");
    
    if (s_font_partition == NULL) {
        ESP_LOGE(TAG, "Font partition not found");
        return false;
    }
    
    ESP_LOGI(TAG, "Font partition found:");
    ESP_LOGI(TAG, "  Label: %s", s_font_partition->label);
    ESP_LOGI(TAG, "  Offset: 0x%lx", (unsigned long)s_font_partition->address);
    ESP_LOGI(TAG, "  Size: %lu bytes (%.2f MB)", 
             (unsigned long)s_font_partition->size,
             s_font_partition->size / (1024.0 * 1024.0));
    
    // 计算实际需要 mmap 的大小（不超过实际字体数据大小）
    size_t mmap_size = FONT_MMAP_SIZE;
    if (mmap_size > s_font_partition->size) {
        mmap_size = s_font_partition->size;
    }
    
    ESP_LOGI(TAG, "Attempting to mmap %lu bytes (%.2f MB) of font data...", 
             (unsigned long)mmap_size, mmap_size / (1024.0 * 1024.0));
    
    // 使用 mmap 将字体分区映射到虚拟地址（零拷贝读取）
    // 注意：只映射实际需要的大小，而不是整个 5MB 分区
    esp_err_t err = esp_partition_mmap(s_font_partition, 0, mmap_size,
                                        SPI_FLASH_MMAP_DATA, 
                                        (const void **)&s_font_mmap_ptr, 
                                        &s_font_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap font partition (%lu bytes): %s", 
                 (unsigned long)mmap_size, esp_err_to_name(err));
        
        // 如果仍然失败，尝试更小的大小（3MB）
        ESP_LOGW(TAG, "Retrying with 3MB mmap size...");
        mmap_size = 3 * 1024 * 1024;
        err = esp_partition_mmap(s_font_partition, 0, mmap_size,
                                 SPI_FLASH_MMAP_DATA, 
                                 (const void **)&s_font_mmap_ptr, 
                                 &s_font_mmap_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mmap even with 3MB: %s", esp_err_to_name(err));
            return false;
        }
    }
    
    ESP_LOGI(TAG, "Font partition mmap'ed at %p (zero-copy enabled, size=%.2f MB)", 
             (void*)s_font_mmap_ptr, mmap_size / (1024.0 * 1024.0));
    
    // 检查分区大小是否足够存储完整字体
    size_t required_size = FONT_TOTAL_CHARS * FONT_GLYPH_SIZE;
    if (s_font_partition->size < required_size) {
        ESP_LOGW(TAG, "Font partition size (%lu) is smaller than required (%lu)",
                 (unsigned long)s_font_partition->size,
                 (unsigned long)required_size);
    }
    
    return true;
}

bool font_partition_is_available(void)
{
    return s_font_partition != NULL && s_font_mmap_ptr != NULL;
}

/**
 * @brief 获取字形数据的直接指针（零拷贝）
 * @param unicode Unicode 码点
 * @return 字形数据指针，失败返回 NULL
 */
const uint8_t* font_partition_get_glyph_ptr(uint32_t unicode)
{
    if (s_font_mmap_ptr == NULL) {
        return NULL;
    }
    
    if (unicode >= FONT_TOTAL_CHARS) {
        return NULL;
    }
    
    size_t offset = unicode * FONT_GLYPH_SIZE;
    if (offset + FONT_GLYPH_SIZE > s_font_partition->size) {
        return NULL;
    }
    
    return s_font_mmap_ptr + offset;
}

size_t font_partition_read_glyph(uint32_t unicode, uint8_t *buffer, size_t glyph_size)
{
    if (s_font_mmap_ptr == NULL) {
        ESP_LOGE(TAG, "Font partition not mmap'ed");
        return 0;
    }
    
    if (buffer == NULL || glyph_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return 0;
    }
    
    // 检查 Unicode 范围
    if (unicode >= FONT_TOTAL_CHARS) {
        ESP_LOGW(TAG, "Unicode out of range: 0x%lx", (unsigned long)unicode);
        return 0;
    }
    
    // 计算字形在分区中的偏移
    size_t offset = unicode * FONT_GLYPH_SIZE;
    
    // 检查是否超出分区范围
    if (offset + glyph_size > s_font_partition->size) {
        ESP_LOGE(TAG, "Read would exceed partition bounds");
        return 0;
    }
    
    // 直接从 mmap 地址复制（比 esp_partition_read 更快）
    memcpy(buffer, s_font_mmap_ptr + offset, glyph_size);
    
    return glyph_size;
}

bool font_partition_is_valid(void)
{
    if (s_font_mmap_ptr == NULL) {
        return false;
    }

    // 使用 mmap 直接检查，无需复制到缓冲区
    // Check 0x0000 - 如果全是 0xFF，说明分区未写入数据
    const uint8_t *glyph_0 = s_font_mmap_ptr;
    
    bool all_ff = true;
    for (int i = 0; i < FONT_GLYPH_SIZE; i++) {
        if (glyph_0[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    
    if (all_ff) {
        ESP_LOGW(TAG, "Font partition appears to be erased (all 0xFF)");
        return false;
    }

    return true;
}

void font_partition_get_info(size_t *out_size, size_t *out_offset)
{
    if (s_font_partition != NULL) {
        if (out_size) {
            *out_size = s_font_partition->size;
        }
        if (out_offset) {
            *out_offset = s_font_partition->address;
        }
    } else {
        if (out_size) {
            *out_size = 0;
        }
        if (out_offset) {
            *out_offset = 0;
        }
    }
}
