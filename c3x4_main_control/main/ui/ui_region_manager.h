/**
 * @file ui_region_manager.h
 * @brief UI 区域管理器 - 管理需要更新的UI区域，支持焦点高亮和顺序局刷
 *
 * 功能：
 * - 记录多个需要重绘的区域
 * - 自动计算旧焦点、新焦点的更新区域
 * - 支持颜色翻转绘制（焦点高亮）
 * - 按顺序执行局部刷新
 */

#ifndef UI_REGION_MANAGER_H
#define UI_REGION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

// 最大支持的区域数量
#define MAX_UPDATE_REGIONS  8

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief UI 区域
 */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    bool valid;
} ui_region_t;

/**
 * @brief 区域绘制回调函数
 * @param region 需要绘制的区域
 * @param user_data 用户数据
 */
typedef void (*region_draw_callback_t)(const ui_region_t *region, void *user_data);

/**
 * @brief 区域管理器句柄
 */
typedef struct {
    ui_region_t regions[MAX_UPDATE_REGIONS];  // 待更新区域列表
    int region_count;                          // 区域数量
    bool auto_refresh;                         // 是否自动刷新
} ui_region_manager_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化区域管理器
 * @param manager 区域管理器
 * @param auto_refresh 是否自动刷新（完成绘制后立即局刷）
 */
void ui_region_manager_init(ui_region_manager_t *manager, bool auto_refresh);

/**
 * @brief 清空所有区域
 * @param manager 区域管理器
 */
void ui_region_manager_clear(ui_region_manager_t *manager);

/**
 * @brief 添加一个需要更新的区域
 * @param manager 区域管理器
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 * @return true 成功，false 失败（区域列表已满）
 */
bool ui_region_manager_add_region(ui_region_manager_t *manager, 
                                   int x, int y, int width, int height);

/**
 * @brief 添加焦点变化区域（自动计算旧焦点+新焦点）
 * @param manager 区域管理器
 * @param old_x 旧焦点 X（-1 表示无旧焦点）
 * @param old_y 旧焦点 Y
 * @param old_width 旧焦点宽度
 * @param old_height 旧焦点高度
 * @param new_x 新焦点 X
 * @param new_y 新焦点 Y
 * @param new_width 新焦点宽度
 * @param new_height 新焦点高度
 * @return true 成功，false 失败
 */
bool ui_region_manager_add_focus_change(ui_region_manager_t *manager,
                                         int old_x, int old_y, int old_width, int old_height,
                                         int new_x, int new_y, int new_width, int new_height);

/**
 * @brief 绘制所有区域并按顺序局部刷新
 * @param manager 区域管理器
 * @param draw_callback 绘制回调函数（每个区域调用一次）
 * @param user_data 用户数据
 */
void ui_region_manager_draw_and_refresh(ui_region_manager_t *manager,
                                        region_draw_callback_t draw_callback,
                                        void *user_data);

/**
 * @brief 获取区域数量
 * @param manager 区域管理器
 * @return 区域数量
 */
int ui_region_manager_get_count(const ui_region_manager_t *manager);

/**
 * @brief 获取指定索引的区域
 * @param manager 区域管理器
 * @param index 索引
 * @return 区域指针，如果索引无效返回 NULL
 */
const ui_region_t* ui_region_manager_get_region(const ui_region_manager_t *manager, int index);

/**
 * @brief 合并重叠或相邻的区域（优化刷新）
 * @param manager 区域管理器
 */
void ui_region_manager_merge_regions(ui_region_manager_t *manager);

#endif // UI_REGION_MANAGER_H
