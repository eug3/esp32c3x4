/**
 * @file gb18030_conv.c
 * @brief GB18030/GBK to UTF-8 conversion implementation
 *
 * Uses a lookup table stored in Flash for accurate GBK to Unicode conversion.
 * The table is stored in the 'gbk_table' partition and mapped directly from Flash.
 *
 * This converter handles:
 * - ASCII (0x00-0x7F): Direct pass-through
 * - GBK double-byte (0x81-0xFE, 0x40-0xFE): Full GBK character set
 */

#include "gb18030_conv.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include <string.h>

static const char *TAG = "GB18030_CONV";

// GBK table partition handle and pointer
static const esp_partition_t *s_gbk_partition = NULL;
static const uint8_t *s_gbk_table_mapped = NULL;
static spi_flash_mmap_handle_t s_gbk_mmap_handle;

// Table parameters (must match generate_gbk_table.py)
#define GBK_TABLE_START   0x8140   // First valid GBK code
#define GBK_TABLE_END     0xFEFE   // Last valid GBK code
#define GBK_TABLE_SIZE    (GBK_TABLE_END - GBK_TABLE_START + 1)  // 32,191 entries

/**
 * @brief Initialize the GBK lookup table from Flash partition
 * @return true if table loaded successfully
 */
static bool gbk_table_init(void)
{
    if (s_gbk_table_mapped != NULL) {
        return true;  // Already initialized
    }

    // Find the GBK table partition (search all data subtypes)
    s_gbk_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                ESP_PARTITION_SUBTYPE_ANY,
                                                "gbk_table");
    if (s_gbk_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find 'gbk_table' partition");
        return false;
    }

    // Map the partition to memory (read-only, cached)
    const void *mapped_ptr = NULL;
    esp_err_t err = esp_partition_mmap(s_gbk_partition, 0, s_gbk_partition->size,
                                        SPI_FLASH_MMAP_DATA, &mapped_ptr, &s_gbk_mmap_handle);
    if (err != ESP_OK || mapped_ptr == NULL) {
        ESP_LOGE(TAG, "Failed to map 'gbk_table' partition to memory: %d", err);
        return false;
    }

    s_gbk_table_mapped = (const uint8_t *)mapped_ptr;

    ESP_LOGI(TAG, "GBK lookup table loaded from Flash: addr=%p, size=%lu",
             (void *)s_gbk_table_mapped, (unsigned long)s_gbk_partition->size);
    return true;
}

/**
 * @brief Convert GBK double-byte character to Unicode codepoint using Flash lookup table
 * @param gb_high High byte (0x81-0xFE)
 * @param gb_low Low byte (0x40-0xFE, excluding 0x7F)
 * @return Unicode codepoint, or 0 if invalid/unmapped
 */
static uint32_t gbk_to_unicode(uint8_t gb_high, uint8_t gb_low)
{
    // Validate input range
    if (gb_high < 0x81 || gb_high > 0xFE) {
        return 0;
    }
    if (gb_low < 0x40 || gb_low > 0xFE || gb_low == 0x7F) {
        return 0;
    }

    // Calculate GBK code
    uint16_t gbk_code = (gb_high << 8) | gb_low;

    // Check if within table range
    if (gbk_code < GBK_TABLE_START || gbk_code > GBK_TABLE_END) {
        return 0;
    }

    // Read from Flash lookup table
    // Each entry is 2 bytes (16-bit Unicode)
    if (s_gbk_table_mapped == NULL) {
        // Initialize table on first use if not already done
        if (!gbk_table_init()) {
            ESP_LOGW(TAG, "GBK table not available");
            return 0;
        }
    }

    uint16_t table_offset = gbk_code - GBK_TABLE_START;
    uint16_t unicode = (s_gbk_table_mapped[table_offset * 2] << 8) |
                       s_gbk_table_mapped[table_offset * 2 + 1];

    return unicode;
}

/**
 * @brief Convert Unicode codepoint to UTF-8
 * @param unicode Unicode codepoint
 * @param utf8_out Output buffer (must have space for at least 4 bytes)
 * @return Number of bytes written (1-4), or 0 on error
 */
static int unicode_to_utf8(uint32_t unicode, char *utf8_out)
{
    if (utf8_out == NULL) {
        return 0;
    }

    if (unicode < 0x80) {
        // 1-byte UTF-8 (ASCII)
        utf8_out[0] = (char)unicode;
        return 1;
    } else if (unicode < 0x800) {
        // 2-byte UTF-8
        utf8_out[0] = (char)(0xC0 | (unicode >> 6));
        utf8_out[1] = (char)(0x80 | (unicode & 0x3F));
        return 2;
    } else if (unicode < 0x10000) {
        // 3-byte UTF-8 (most Chinese characters)
        utf8_out[0] = (char)(0xE0 | (unicode >> 12));
        utf8_out[1] = (char)(0x80 | ((unicode >> 6) & 0x3F));
        utf8_out[2] = (char)(0x80 | (unicode & 0x3F));
        return 3;
    } else if (unicode < 0x110000) {
        // 4-byte UTF-8
        utf8_out[0] = (char)(0xF0 | (unicode >> 18));
        utf8_out[1] = (char)(0x80 | ((unicode >> 12) & 0x3F));
        utf8_out[2] = (char)(0x80 | ((unicode >> 6) & 0x3F));
        utf8_out[3] = (char)(0x80 | (unicode & 0x3F));
        return 4;
    }

    return 0;
}

int gb18030_char_bytes(const uint8_t *gb_text)
{
    if (gb_text == NULL) {
        return 0;
    }

    uint8_t c = gb_text[0];

    // ASCII
    if (c < 0x80) {
        return 1;
    }

    // GBK/GB18030 double-byte
    if (c >= 0x81 && c <= 0xFE) {
        uint8_t next = gb_text[1];
        if (next >= 0x40 && next <= 0xFE && next != 0x7F) {
            return 2;
        }
        // Invalid second byte
        return 1;  // Treat as single byte and skip
    }

    // Invalid
    return 1;
}

int gb18030_to_utf8(const uint8_t *gb_text, size_t gb_len,
                    char *utf8_text, size_t utf8_size)
{
    if (gb_text == NULL || utf8_text == NULL || utf8_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Initialize GBK table on first use
    if (s_gbk_table_mapped == NULL) {
        if (!gbk_table_init()) {
            ESP_LOGE(TAG, "Failed to initialize GBK table");
            return -1;
        }
    }

    size_t gb_pos = 0;
    size_t utf8_pos = 0;

    while (gb_pos < gb_len && utf8_pos < utf8_size - 5) {  // Reserve 4 bytes for UTF-8 + 1 for null
        uint8_t c = gb_text[gb_pos];

        if (c == 0) {
            // Null terminator
            break;
        }

        if (c < 0x80) {
            // ASCII - copy directly
            utf8_text[utf8_pos++] = (char)c;
            gb_pos++;
        } else if (c >= 0x81 && c <= 0xFE && gb_pos + 1 < gb_len) {
            // GBK/GB18030 double-byte character
            uint8_t next = gb_text[gb_pos + 1];

            if (next >= 0x40 && next <= 0xFE && next != 0x7F) {
                // Valid GBK character
                uint32_t unicode = gbk_to_unicode(c, next);

                if (unicode > 0) {
                    // Convert to UTF-8
                    int utf8_len = unicode_to_utf8(unicode, &utf8_text[utf8_pos]);
                    if (utf8_len > 0) {
                        utf8_pos += utf8_len;
                    } else {
                        // Conversion failed - use replacement character
                        utf8_text[utf8_pos++] = '?';
                    }
                } else {
                    // Unmapped character - use replacement
                    utf8_text[utf8_pos++] = '?';
                }

                gb_pos += 2;
            } else {
                // Invalid second byte - skip first byte
                utf8_text[utf8_pos++] = '?';
                gb_pos++;
            }
        } else {
            // Invalid character - skip
            utf8_text[utf8_pos++] = '?';
            gb_pos++;
        }
    }

    // Null terminate
    if (utf8_pos < utf8_size) {
        utf8_text[utf8_pos] = '\0';
    } else {
        utf8_text[utf8_size - 1] = '\0';
        utf8_pos = utf8_size - 1;
    }

    return (int)utf8_pos;
}
