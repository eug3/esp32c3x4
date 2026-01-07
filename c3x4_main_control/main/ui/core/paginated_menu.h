/**
 * @file paginated_menu.h
 * @brief 通用分页菜单组件 - 翻页模式
 *
 * 提供分页列表菜单功能，支持：
 * - 按页显示，每页固定条目数
 * - 上一页/下一页翻页
 * - 选中项管理（在当前页内）
 * - 统一的绘制接口
 */

#ifndef PAGINATED_MENU_H
#define PAGINATED_MENU_H

#include "display_engine.h"
#include "input_handler.h"
#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

// 默认布局参数
#define PAGINATED_MENU_DEFAULT_START_Y         80    // 列表起始 Y 坐标
#define PAGINATED_MENU_DEFAULT_ITEM_HEIGHT    50    // 每项高度
#define PAGINATED_MENU_DEFAULT_BOTTOM_MARGIN  80    // 底部边距（预留页码显示区域）
#define PAGINATED_MENU_DEFAULT_MENU_WIDTH     400   // 菜单宽度
#define PAGINATED_MENU_DEFAULT_TEXT_OFFSET_Y  10    // 文本 Y 偏移（相对于 item_y）
#define PAGINATED_MENU_DEFAULT_ITEMS_PER_PAGE  10    // 每页条目数（默认）

// 页码显示位置
#define PAGINATED_MENU_PAGE_HINT_X  -1    // -1 表示右下角自动定位
#define PAGINATED_MENU_PAGE_HINT_Y  -1    // -1 表示底部自动定位

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 菜单项数据获取回调
 *
 * @param index 条目索引（总索引，从 0 开始）
 * @param out_text 输出：条目文本（缓冲区大小由调用者保证）
 * @param out_text_size 文本缓冲区大小
 * @param out_is_selected 输出：是否为选中项
 * @return true 成功，false 索引超出范围
 */
typedef bool (*paginated_menu_item_getter_t)(int index, char *out_text, int out_text_size, bool *out_is_selected);

/**
 * @brief 菜单项绘制回调（可选，用于自定义绘制）
 *
 * @param visible_index 可见索引（当前页内的索引，0 到 items_per_page-1）
 * @param total_index 总索引（全局索引）
 * @param x 条目 X 坐标
 * @param y 条目 Y 坐标
 * @param width 条目宽度
 * @param height 条目高度
 * @param is_selected 是否选中
 * @param user_data 用户数据
 */
typedef void (*paginated_menu_item_drawer_t)(int visible_index, int total_index,
                                             int x, int y, int width, int height,
                                             bool is_selected, void *user_data);

/**
 * @brief 分页菜单配置
 */
typedef struct {
    int start_y;              // 列表起始 Y 坐标
    int item_height;           // 每项高度
    int bottom_margin;         // 底部边距
    int menu_width;            // 菜单宽度
    int text_offset_y;         // 文本 Y 偏移
    int items_per_page;        // 每页条目数

    // 数据回调
    paginated_menu_item_getter_t item_getter;     // 获取条目文本（必需）
    paginated_menu_item_drawer_t item_drawer;      // 自定义绘制（可选，NULL 使用默认）
    void *user_data;                            // 传递给回调的用户数据

    // 样式配置
    int padding_x;             // 水平内边距（默认 10）
    int padding_y;             // 垂直内边距（默认 5）

    // 页码显示配置
    bool show_page_hint;       // 是否显示页码（默认 true）
    int page_hint_x;           // 页码 X 坐标（-1 自动定位到右下角）
    int page_hint_y;           // 页码 Y 坐标（-1 自动定位到底部）
} paginated_menu_config_t;

/**
 * @brief 分页菜单状态
 */
typedef struct {
    int total_count;           // 总条目数
    int selected_index;        // 当前选中索引（0 到 total_count-1）
    int current_page;          // 当前页码（从 0 开始）
    int items_per_page;        // 每页条目数
    int total_pages;           // 总页数
} paginated_menu_state_t;

/**
 * @brief 分页菜单实例
 */
typedef struct {
    paginated_menu_config_t config;
    paginated_menu_state_t state;
    bool initialized;
} paginated_menu_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化分页菜单
 * @param menu 菜单实例
 * @param config 配置（NULL 使用默认配置）
 * @return true 成功，false 失败
 */
bool paginated_menu_init(paginated_menu_t *menu, const paginated_menu_config_t *config);

/**
 * @brief 反初始化分页菜单
 * @param menu 菜单实例
 */
void paginated_menu_deinit(paginated_menu_t *menu);

/**
 * @brief 设置总条目数
 * @param menu 菜单实例
 * @param total_count 总条目数
 */
void paginated_menu_set_total_count(paginated_menu_t *menu, int total_count);

/**
 * @brief 获取总条目数
 * @param menu 菜单实例
 * @return 总条目数
 */
int paginated_menu_get_total_count(const paginated_menu_t *menu);

/**
 * @brief 设置选中索引
 * @param menu 菜单实例
 * @param index 选中索引
 * @return true 成功，false 索引超出范围
 */
bool paginated_menu_set_selected_index(paginated_menu_t *menu, int index);

/**
 * @brief 获取选中索引
 * @param menu 菜单实例
 * @return 选中索引
 */
int paginated_menu_get_selected_index(const paginated_menu_t *menu);

/**
 * @brief 获取当前页码（从 1 开始）
 * @param menu 菜单实例
 * @return 当前页码
 */
int paginated_menu_get_current_page(const paginated_menu_t *menu);

/**
 * @brief 获取总页数
 * @param menu 菜单实例
 * @return 总页数
 */
int paginated_menu_get_total_pages(const paginated_menu_t *menu);

/**
 * @brief 获取每页条目数
 * @param menu 菜单实例
 * @return 每页条目数
 */
int paginated_menu_get_items_per_page(const paginated_menu_t *menu);

/**
 * @brief 翻到指定页
 * @param menu 菜单实例
 * @param page 页码（从 1 开始）
 * @return true 成功，false 页码超出范围
 */
bool paginated_menu_goto_page(paginated_menu_t *menu, int page);

/**
 * @brief 上一页
 * @param menu 菜单实例
 * @return true 成功，false 已在第一页
 */
bool paginated_menu_prev_page(paginated_menu_t *menu);

/**
 * @brief 下一页
 * @param menu 菜单实例
 * @return true 成功，false 已在最后一页
 */
bool paginated_menu_next_page(paginated_menu_t *menu);

/**
 * @brief 在当前页内移动选中项
 * @param menu 菜单实例
 * @param delta 移动量（正数向下，负数向上）
 * @return true 成功移动，false 已到边界
 */
bool paginated_menu_move_selection(paginated_menu_t *menu, int delta);

/**
 * @brief 绘制菜单
 * @param menu 菜单实例
 */
void paginated_menu_draw(paginated_menu_t *menu);

/**
 * @brief 绘制单个条目（用于局部刷新）
 * @param menu 菜单实例
 * @param visible_index 可见索引（当前页内的索引）
 */
void paginated_menu_draw_item(paginated_menu_t *menu, int visible_index);

/**
 * @brief 显示页码提示（如 "1/3"）
 * @param menu 菜单实例
 */
void paginated_menu_draw_page_hint(paginated_menu_t *menu);

/**
 * @brief 显示底部操作提示
 * @param menu 菜单实例
 * @param hint_text 提示文本（如 "上下: 选择  确认: 进入  返回: 返回"）
 * @param x X 坐标（-1 使用默认左下角）
 * @param y Y 坐标（-1 使用默认左下角）
 */
void paginated_menu_draw_footer_hint(paginated_menu_t *menu, const char *hint_text, int x, int y);

/**
 * @brief 处理按钮事件
 * @param menu 菜单实例
 * @param btn 按钮
 * @param old_index 旧选中索引（输出，可为 NULL）
 * @param new_index 新选中索引（输出，可为 NULL）
 * @return true 状态已改变，false 无变化
 */
bool paginated_menu_handle_button(paginated_menu_t *menu, button_t btn,
                                  int *old_index, int *new_index);

#endif // PAGINATED_MENU_H
