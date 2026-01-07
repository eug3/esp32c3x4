/**
 * @file wallpaper_manager.h
 * @brief 壁纸管理模块
 *
 * 功能：
 * - 扫描 SD 卡图片
 * - 图片解码并转换为 4-bit 灰度位图
 * - 缓存到 LittleFS
 * - 轻度休眠时显示壁纸
 */

#ifndef WALLPAPER_MANAGER_H
#define WALLPAPER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// 壁纸信息
typedef struct {
    char name[64];           // 显示名称
    char path[128];          // 原始文件路径
    char cache_path[128];    // 缓存文件路径
    uint16_t width;          // 位图宽度
    uint16_t height;         // 位图高度
    uint32_t file_size;      // 原始文件大小
    uint32_t cache_size;     // 缓存文件大小
} wallpaper_info_t;

// 壁纸列表
typedef struct {
    wallpaper_info_t *items;
    uint16_t count;
    uint16_t capacity;
} wallpaper_list_t;

/**
 * @brief 初始化壁纸管理器
 * @return true 成功，false 失败
 */
bool wallpaper_manager_init(void);

/**
 * @brief 反初始化壁纸管理器
 */
void wallpaper_manager_deinit(void);

/**
 * @brief 扫描 SD 卡获取所有图片
 * @param list 输出壁纸列表
 * @return 扫描到的壁纸数量
 */
int wallpaper_scan_sdcard(wallpaper_list_t *list);

/**
 * @brief 扫描并导入所有壁纸（扫描 + 缓存）
 * @return 导入的壁纸数量
 */
int wallpaper_import_all(void);

/**
 * @brief 获取缓存的壁纸列表
 * @param list 输出壁纸列表
 * @return 缓存的壁纸数量
 */
int wallpaper_get_cached_list(wallpaper_list_t *list);

/**
 * @brief 选择壁纸
 * @param name 壁纸名称
 * @return true 成功，false 失败
 */
bool wallpaper_select(const char *name);

/**
 * @brief 通过完整路径选择壁纸（推荐）
 * @param path SD 卡上的图片完整路径
 * @return true 成功，false 失败
 */
bool wallpaper_select_path(const char *path);

/**
 * @brief 获取当前选中的壁纸
 * @return 壁纸名称，NULL 表示使用默认
 */
const char* wallpaper_get_selected(void);

/**
 * @brief 获取当前选中壁纸的完整路径
 * @return 路径字符串，NULL 表示无
 */
const char* wallpaper_get_selected_path(void);

/**
 * @brief 显示当前选中的壁纸
 * @return true 成功，false 失败
 */
bool wallpaper_show(void);

/**
 * @brief 清除壁纸（恢复阅读界面）
 * @return true 成功，false 失败
 */
bool wallpaper_clear(void);

/**
 * @brief 删除缓存的壁纸
 * @param name 壁纸名称
 * @return true 成功，false 失败
 */
bool wallpaper_delete_cache(const char *name);

/**
 * @brief 清除所有缓存
 * @return true 成功，false 失败
 */
bool wallpaper_clear_all_cache(void);

/**
 * @brief 释放壁纸列表内存
 */
void wallpaper_list_free(wallpaper_list_t *list);

#endif // WALLPAPER_MANAGER_H
