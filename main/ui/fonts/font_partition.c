/**
 * @file font_partition.c
 * @brief 字体分区管理实现
 */

#include "font_partition.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "FONT_PART";

// 字体分区句柄
static const esp_partition_t *s_font_partition = NULL;

// 字体文件信息（假设 19x25 字体，65536 字符）
#define FONT_TOTAL_CHARS 0x10000u
#define FONT_GLYPH_SIZE 75u  // 19x25: width_byte=3, height=25, 3*25=75

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
    return s_font_partition != NULL;
}

size_t font_partition_read_glyph(uint32_t unicode, uint8_t *buffer, size_t glyph_size)
{
    if (s_font_partition == NULL) {
        ESP_LOGE(TAG, "Font partition not initialized");
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
    
    // 检查字形大小
    if (glyph_size != FONT_GLYPH_SIZE) {
        ESP_LOGW(TAG, "Glyph size mismatch: expected %u, got %zu", 
                 FONT_GLYPH_SIZE, glyph_size);
    }
    
    // 计算字形在分区中的偏移
    size_t offset = unicode * FONT_GLYPH_SIZE;
    
    // 检查是否超出分区范围
    if (offset + glyph_size > s_font_partition->size) {
        ESP_LOGE(TAG, "Read would exceed partition bounds");
        return 0;
    }
    
    // 从分区读取数据
    esp_err_t err = esp_partition_read(s_font_partition, offset, buffer, glyph_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from partition: %s", esp_err_to_name(err));
        return 0;
    }
    
    return glyph_size;
}

bool font_partition_is_valid(void)
{
    if (s_font_partition == NULL) {
        return false;
    }

    // Check if partition is erased (all 0xFF)
    // We check the first char (0x0000)
    // If it is all 0xFF, it's likely an erased partition.
    // A valid font (0x0000) should be all 0x00.

    uint8_t buffer[FONT_GLYPH_SIZE];
    
    // Check 0x0000
    if (esp_partition_read(s_font_partition, 0, buffer, FONT_GLYPH_SIZE) != ESP_OK) {
        return false;
    }
    
    bool all_ff = true;
    for (int i = 0; i < FONT_GLYPH_SIZE; i++) {
        if (buffer[i] != 0xFF) {
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
