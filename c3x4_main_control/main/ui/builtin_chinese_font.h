/**
 * @file builtin_chinese_font.h
 * @brief Built-in Chinese font declaration
 */

#ifndef BUILTIN_CHINESE_FONT_H
#define BUILTIN_CHINESE_FONT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Built-in Chinese font - 16px */
extern const lv_font_t lv_font_builtin_chinese_16;

/**
 * @brief Get the built-in Chinese font
 * @return Pointer to the built-in LVGL font
 */
static inline const lv_font_t* get_builtin_chinese_font(void)
{
    return &lv_font_builtin_chinese_16;
}

/**
 * @brief Check if built-in Chinese font is available
 * @return Always returns true (font is compiled in)
 */
static inline bool builtin_font_is_available(void)
{
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_CHINESE_FONT_H */
