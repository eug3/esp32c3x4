/**
 * @file font_manager.h
 * @brief 字体管理器 - 统一的字体管理接口
 */

#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include "lvgl.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
#define FONT_MANAGER_DEFAULT_DIR "/sdcard/字体"
#define NVS_KEY_CURRENT_FONT "current_font"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化字体管理器
 * @return true 成功，false 失败
 */
bool font_manager_init(void);

/**
 * @brief 从 NVS 加载保存的字体选择
 */
void font_manager_load_selection(void);

/**
 * @brief 保存当前字体选择到 NVS
 */
void font_manager_save_selection(void);

/**
 * @brief 获取当前应用的字体
 * @return 当前字体指针
 */
lv_font_t* font_manager_get_font(void);

/**
 * @brief 设置当前字体（通过索引）
 * @param index 字体索引
 * @return true 成功，false 失败
 */
bool font_manager_set_font_by_index(int index);

/**
 * @brief 设置当前字体（通过字体指针）
 * @param font 字体指针
 */
void font_manager_set_font(lv_font_t *font);

/**
 * @brief 获取字体列表
 * @return 字体信息数组指针
 */
const void* font_manager_get_font_list(void);

/**
 * @brief 获取字体数量
 * @return 字体总数
 */
int font_manager_get_font_count(void);

/**
 * @brief 刷新所有使用字体的 UI 元素
 * 用于字体切换后更新显示
 */
void font_manager_refresh_ui(void);

/**
 * @brief 清理字体管理器
 */
void font_manager_cleanup(void);

#endif // FONT_MANAGER_H
