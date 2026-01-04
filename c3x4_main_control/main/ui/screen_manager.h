/**
 * @file screen_manager.h
 * @brief 屏幕导航管理器 - 处理不同屏幕之间的切换（手绘 UI 版本，无 LVGL 依赖）
 */

#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include "input_handler.h"
#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

// 最大屏幕数量
#define MAX_SCREENS 8

// 导航栈深度
#define NAV_STACK_DEPTH 10

/**********************
 *      TYPEDEFS
 **********************/

// 前向声明
typedef struct screen_s screen_t;

/**
 * @brief 屏幕生命周期函数
 */
typedef struct screen_s {
    const char *name;               // 屏幕名称
    void *user_data;                // 用户数据

    // 生命周期函数
    void (*on_show)(screen_t *screen);           // 显示时调用
    void (*on_hide)(screen_t *screen);           // 隐藏时调用
    void (*on_draw)(screen_t *screen);           // 绘制时调用
    void (*on_event)(screen_t *screen, button_t btn, button_event_t event);  // 事件处理

    // 状态
    bool is_visible;                // 是否可见
    bool needs_redraw;              // 是否需要重绘
} screen_t;

/**
 * @brief 屏幕上下文 - 包含系统状态信息
 */
typedef struct screen_context {
    uint32_t battery_mv;
    uint8_t battery_pct;
    bool charging;
    const char *version_str;

    // 函数指针 - 用于获取系统状态
    uint32_t (*read_battery_voltage_mv)(void);
    uint8_t (*read_battery_percentage)(void);
    bool (*is_charging)(void);
} screen_context_t;

/**
 * @brief 屏幕管理器状态
 */
typedef struct {
    screen_t *screens[MAX_SCREENS]; // 所有屏幕
    int screen_count;               // 屏幕数量
    screen_t *current_screen;       // 当前屏幕
    screen_context_t *context;      // 系统上下文

    // 导航栈
    screen_t *nav_stack[NAV_STACK_DEPTH];
    int nav_stack_top;
} screen_manager_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化屏幕管理器
 * @param ctx 屏幕上下文指针
 * @return true 成功，false 失败
 */
bool screen_manager_init(screen_context_t *ctx);

/**
 * @brief 反初始化屏幕管理器
 */
void screen_manager_deinit(void);

/**
 * @brief 注册屏幕
 * @param screen 屏幕指针
 * @return true 成功，false 失败
 */
bool screen_manager_register(screen_t *screen);

/**
 * @brief 注销屏幕
 * @param screen 屏幕指针
 */
void screen_manager_unregister(screen_t *screen);

/**
 * @brief 显示指定屏幕
 * @param screen_name 屏幕名称
 * @return true 成功，false 失败
 */
bool screen_manager_show(const char *screen_name);

/**
 * @brief 显示屏幕（通过指针）
 * @param screen 屏幕指针
 * @return true 成功，false 失败
 */
bool screen_manager_show_screen(screen_t *screen);

/**
 * @brief 返回上一个屏幕
 * @return true 成功，false 失败
 */
bool screen_manager_back(void);

/**
 * @brief 获取当前屏幕
 * @return 当前屏幕指针，NULL 表示无屏幕
 */
screen_t* screen_manager_get_current(void);

/**
 * @brief 查找屏幕
 * @param screen_name 屏幕名称
 * @return 屏幕指针，未找到返回 NULL
 */
screen_t* screen_manager_find(const char *screen_name);

/**
 * @brief 请求重绘当前屏幕
 */
void screen_manager_request_redraw(void);

/**
 * @brief 处理按键事件
 * @param btn 按键
 * @param event 事件
 * @return true 事件已处理，false 未处理
 */
bool screen_manager_handle_event(button_t btn, button_event_t event);

/**
 * @brief 绘制当前屏幕
 */
void screen_manager_draw(void);

/**
 * @brief 获取屏幕管理器状态
 * @return 屏幕管理器状态指针
 */
const screen_manager_t* screen_manager_get_state(void);

/**
 * @brief 获取屏幕上下文
 * @return 屏幕上下文指针
 */
screen_context_t* screen_manager_get_context(void);

// 兼容旧 API 的包装函数

/**
 * @brief 显示首页（兼容旧 API）
 */
void screen_manager_show_index(void);

/**
 * @brief 显示文件浏览器（兼容旧 API）
 */
void screen_manager_show_file_browser(void);

/**
 * @brief 显示设置页面（兼容旧 API）
 */
void screen_manager_show_settings(void);

/**
 * @brief 显示阅读器屏幕（兼容旧 API）
 * @param file_path 要打开的书籍文件路径
 */
void screen_manager_show_reader(const char *file_path);

/**
 * @brief 显示图片浏览器屏幕（兼容旧 API）
 * @param directory 要浏览的图片目录
 */
void screen_manager_show_image_browser(const char *directory);

/**
 * @brief 返回上一页（兼容旧 API）
 * @return true 如果成功返回，false 如果已经在首页无法返回
 */
bool screen_manager_go_back(void);

#endif // SCREEN_MANAGER_H
