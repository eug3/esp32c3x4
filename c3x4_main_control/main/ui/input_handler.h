/**
 * @file input_handler.h
 * @brief 按键输入处理模块
 *
 * 按键映射（Xteink X4）：
 * - BTN_RIGHT: 右键（下一页/向下）
 * - BTN_LEFT: 左键（上一页/向上）
 * - BTN_CONFIRM: 确认键（选择）
 * - BTN_BACK: 返回键
 * - BTN_VOLUME_UP: 音量+（也可用于导航）
 * - BTN_VOLUME_DOWN: 音量-（也可用于导航）
 * - BTN_POWER: 电源键
 */

#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

// 按键枚举
typedef enum {
    BTN_NONE = 0,
    BTN_RIGHT,        // 右键
    BTN_LEFT,         // 左键
    BTN_CONFIRM,      // 确认键
    BTN_BACK,         // 返回键
    BTN_VOLUME_UP,    // 音量+
    BTN_VOLUME_DOWN,  // 音量-
    BTN_POWER,        // 电源键
} button_t;

// 按键事件
typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_PRESSED,      // 按下
    BTN_EVENT_RELEASED,     // 释放
    BTN_EVENT_LONG_PRESSED, // 长按
    BTN_EVENT_REPEAT,       // 重复
} button_event_t;

// 防抖时间（ms）
#define BTN_DEBOUNCE_TIME      50

// 长按时间（ms）
#define BTN_LONG_PRESS_TIME    1000

// 重复延迟（ms）
#define BTN_REPEAT_DELAY       300
#define BTN_REPEAT_INTERVAL    150

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 按键回调函数类型
 * @param btn 按键
 * @param event 事件
 * @param user_data 用户数据
 */
typedef void (*button_callback_t)(button_t btn, button_event_t event, void *user_data);

/**
 * @brief 按键处理配置
 */
typedef struct {
    bool enable_debounce;        // 启用防抖
    bool enable_long_press;      // 启用长按检测
    bool enable_repeat;          // 启用重复检测
    uint16_t debounce_ms;        // 防抖时间
    uint16_t long_press_ms;      // 长按时间
    uint16_t repeat_delay_ms;    // 重复延迟
    uint16_t repeat_interval_ms; // 重复间隔
} input_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化输入处理模块
 * @param config 配置（NULL 使用默认配置）
 * @return true 成功，false 失败
 */
bool input_handler_init(const input_config_t *config);

/**
 * @brief 反初始化输入处理模块
 */
void input_handler_deinit(void);

/**
 * @brief 注册按键回调
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return true 成功，false 失败
 */
bool input_handler_register_callback(button_callback_t callback, void *user_data);

/**
 * @brief 取消注册按键回调
 */
void input_handler_unregister_callback(void);

/**
 * @brief 获取当前按键状态（阻塞）
 * @param btn 输出按键
 * @param event 输出事件
 * @param timeout_ms 超时时间（0 表示非阻塞）
 * @return true 有按键事件，false 超时
 */
bool input_handler_get_event(button_t *btn, button_event_t *event, int timeout_ms);

/**
 * @brief 获取按键名称（用于调试）
 * @param btn 按键
 * @return 按键名称字符串
 */
const char* input_handler_get_button_name(button_t btn);

/**
 * @brief 获取事件名称（用于调试）
 * @param event 事件
 * @return 事件名称字符串
 */
const char* input_handler_get_event_name(button_event_t event);

/**
 * @brief 轮询输入处理（在主循环中调用）
 * @note 此函数会检测按键状态并触发回调
 */
void input_handler_poll(void);

/**
 * @brief 等待按键按下（阻塞）
 * @param btn 输出按键
 * @return true 成功，false 超时或错误
 */
bool input_handler_wait_for_button(button_t *btn);

/**
 * @brief 读取原始按键状态（供 main.c 使用）
 * @return 当前按下的按键
 */
button_t read_raw_button(void);

#endif // INPUT_HANDLER_H
