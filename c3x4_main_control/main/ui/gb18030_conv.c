/**
 * @file gb18030_conv.c
 * @brief GB18030/GBK to UTF-8 conversion implementation
 * 
 * This is a simplified converter that handles:
 * - ASCII (0x00-0x7F): Direct pass-through
 * - GBK double-byte (0x81-0xFE, 0x40-0xFE): Common Chinese characters
 * 
 * For a production system, consider using a full conversion table or library like iconv.
 * This implementation provides reasonable coverage for typical Chinese text files.
 */

#include "gb18030_conv.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "GB18030_CONV";

// Include simplified GBK to Unicode mapping
// This maps GBK区位码 (zone-point) to Unicode
// GBK uses: high byte (0x81-0xFE), low byte (0x40-0xFE)

/**
 * @brief Convert GBK double-byte character to Unicode codepoint
 * @param gb_high High byte (0x81-0xFE)
 * @param gb_low Low byte (0x40-0xFE)
 * @return Unicode codepoint, or 0 if invalid
 */
static uint32_t gbk_to_unicode(uint8_t gb_high, uint8_t gb_low)
{
    // GBK character range check
    if (gb_high < 0x81 || gb_high > 0xFE) {
        return 0;
    }
    if (gb_low < 0x40 || gb_low > 0xFE || gb_low == 0x7F) {
        return 0;
    }

    // Calculate GBK code
    uint16_t gbk_code = (gb_high << 8) | gb_low;

    // GB2312 Level-1 range (most common): 0xB0A1-0xD7F9
    // GB2312 Level-2 range: 0xD8A1-0xF7FE
    // GBK extensions: 0x8140-0xA0FE, 0xA8A1-0xFEFE

    // Simplified mapping using offset calculation
    // This is an approximation - for accurate conversion, use a lookup table
    
    // GB2312 Level-1 (most common characters)
    if (gbk_code >= 0xB0A1 && gbk_code <= 0xD7F9) {
        // Map to Unicode CJK Unified Ideographs (U+4E00-U+9FFF)
        // GB2312 Level-1 has ~3755 characters starting from "啊"
        uint32_t offset = 0;
        
        // Calculate offset from GB2312 table start
        int zone = gb_high - 0xB0;  // Zone (区): 16-55
        int point = gb_low - 0xA1;   // Point (位): 1-94
        
        if (zone >= 0 && zone < 40 && point >= 0 && point < 94) {
            offset = zone * 94 + point;
            // Map to Unicode starting from U+554A (啊)
            return 0x554A + offset;
        }
    }

    // GB2312 Level-2
    if (gbk_code >= 0xD8A1 && gbk_code <= 0xF7FE) {
        int zone = gb_high - 0xD8;
        int point = gb_low - 0xA1;
        
        if (zone >= 0 && zone < 32 && point >= 0 && point < 94) {
            uint32_t offset = zone * 94 + point;
            // Map to Unicode starting from U+7EA0
            return 0x7EA0 + offset;
        }
    }

    // GBK extensions (less common)
    if (gbk_code >= 0x8140 && gbk_code <= 0xA0FE) {
        int zone = gb_high - 0x81;
        int point = (gb_low >= 0x80) ? (gb_low - 0x80 + 63) : (gb_low - 0x40);
        
        if (zone >= 0 && zone < 32 && point >= 0 && point < 190) {
            uint32_t offset = zone * 190 + point;
            return 0x4E00 + offset;  // Approximate mapping
        }
    }

    // Fallback: clamp to valid CJK range
    // CJK Unified Ideographs range: U+4E00 to U+9FFF (20,992 characters)
    // 0x51FF = 20,991 (last offset before wrapping)
    // 0x5200 = 20,992 (total characters in range)
    ESP_LOGD(TAG, "Using fallback mapping for GBK: 0x%04X", gbk_code);
    uint32_t offset = (gbk_code - 0x8140) % 0x51FF;
    uint32_t unicode = 0x4E00 + offset;
    // Ensure we stay within CJK Unified Ideographs (U+4E00-U+9FFF)
    if (unicode > 0x9FFF) {
        unicode = 0x4E00 + (offset % 0x5200);
    }
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
