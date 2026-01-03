/**
 * @file font_loader.h
 * @brief LVGL 字体加载器 - 从 SD 卡加载 lv_font_conv 生成的 .bin 字体文件
 */

#ifndef FONT_LOADER_H
#define FONT_LOADER_H

#include "lvgl.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
#define MAX_FONT_NAME_LEN 64
#define MAX_FONTS 10

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 字体信息结构
 */
typedef struct {
    char name[MAX_FONT_NAME_LEN];     // 字体名称（不含路径和扩展名）
    char file_path[256];              // 完整文件路径
    lv_font_t *lv_font;               // LVGL 字体指针
    bool is_loaded;                   // 是否已加载
    int ref_count;                    // 引用计数
} font_info_t;

/**
 * @brief 字体加载器状态
 */
typedef struct {
    font_info_t fonts[MAX_FONTS];     // 字体列表
    int font_count;                   // 已加载字体数量
    char font_dir[256];               // 字体目录路径
    lv_font_t *default_font;          // 默认字体（初始为 montserrat）
    lv_font_t *current_font;          // 当前选择的字体
} font_loader_state_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化字体加载器
 * @param font_dir 字体文件所在目录（如 "/sdcard/字体"）
 * @return true 成功，false 失败
 */
bool font_loader_init(const char *font_dir);

/**
 * @brief 扫描字体目录中的所有 .bin 文件
 * @return 找到的字体文件数量
 */
int font_loader_scan_fonts(void);

/**
 * @brief 从 SD 卡加载字体文件
 * @param file_path 字体文件的完整路径
 * @param font_name 输出参数，字体名称
 * @param name_len 名称缓冲区大小
 * @return LVGL 字体指针，失败返回 NULL
 */
lv_font_t* font_load_from_file(const char *file_path, char *font_name, size_t name_len);

/**
 * @brief 加载指定索引的字体
 * @param index 字体索引（0 ~ font_count-1）
 * @return LVGL 字体指针，失败返回 NULL
 */
lv_font_t* font_load_by_index(int index);

/**
 * @brief 获取字体列表
 * @return 字体信息数组指针
 */
const font_info_t* font_loader_get_font_list(void);

/**
 * @brief 获取字体数量
 * @return 字体总数
 */
int font_loader_get_font_count(void);

/**
 * @brief 设置当前使用的字体
 * @param font 要设置的字体，NULL 表示使用默认字体
 */
void font_loader_set_current_font(lv_font_t *font);

/**
 * @brief 获取当前字体
 * @return 当前字体指针
 */
lv_font_t* font_loader_get_current_font(void);

/**
 * @brief 获取默认字体
 * @return 默认字体指针（montserrat）
 */
lv_font_t* font_loader_get_default_font(void);

/**
 * @brief 通过名称查找字体
 * @param name 字体名称
 * @return 字体信息指针，未找到返回 NULL
 */
const font_info_t* font_loader_find_font_by_name(const char *name);

/**
 * @brief 卸载字体
 * @param font 要卸载的字体
 */
void font_loader_unload_font(lv_font_t *font);

/**
 * @brief 清理所有加载的字体
 */
void font_loader_cleanup(void);

/**
 * @brief 获取字体加载器状态
 * @return 字体加载器状态指针
 */
font_loader_state_t* font_loader_get_state(void);

#endif // FONT_LOADER_H
