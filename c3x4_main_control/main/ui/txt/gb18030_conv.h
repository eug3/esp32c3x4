/**
 * @file gb18030_conv.h  
 * @brief GB18030/GBK to UTF-8 conversion utilities
 * 
 * Simplified converter for common Chinese characters.
 * Supports GB2312 range which covers most common characters.
 */

#ifndef GB18030_CONV_H
#define GB18030_CONV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert GB18030/GBK text buffer to UTF-8
 * @param gb_text Input text in GB18030/GBK encoding
 * @param gb_len Length of input in bytes
 * @param utf8_text Output buffer for UTF-8 text
 * @param utf8_size Size of output buffer
 * @return Number of bytes written to utf8_text, or -1 on error
 * 
 * Note: This handles ASCII and common Chinese characters.
 * Uncommon characters may be replaced with '?'.
 */
int gb18030_to_utf8(const uint8_t *gb_text, size_t gb_len, 
                    char *utf8_text, size_t utf8_size);

/**
 * @brief Get length of next GB18030 character in bytes
 * @param gb_text Pointer to GB18030 text
 * @return 1 for ASCII, 2 for double-byte character, 0 for invalid
 */
int gb18030_char_bytes(const uint8_t *gb_text);

#ifdef __cplusplus
}
#endif

#endif // GB18030_CONV_H
