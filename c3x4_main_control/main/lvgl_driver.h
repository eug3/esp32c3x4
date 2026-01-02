/**
 * @file lvgl_driver.h
 * @brief LVGL驱动适配层头文件
 */

#ifndef LVGL_DRIVER_H
#define LVGL_DRIVER_H

#include "lvgl.h"

// 按钮枚举定义（从main.c复制）
typedef enum {
    BTN_NONE = 0,
    BTN_RIGHT,
    BTN_LEFT,
    BTN_CONFIRM,
    BTN_BACK,
    BTN_VOLUME_UP,
    BTN_VOLUME_DOWN,
    BTN_POWER
} button_t;

// 外部函数声明：从main.c中读取按钮状态
button_t get_pressed_button(void);

// 刷新模式枚举（从lvgl_driver.c导出）
typedef enum {
    EPD_REFRESH_PARTIAL = 0,  // 局部刷新 - 最快，可能有鬼影
    EPD_REFRESH_FAST = 1,     // 快速刷新 - 全屏快速模式，平衡速度和清晰度
    EPD_REFRESH_FULL = 2      // 全刷 - 最清晰，速度最慢
} epd_refresh_mode_t;

/**
 * @brief 初始化LVGL显示驱动
 * @return 显示设备指针
 */
lv_display_t* lvgl_display_init(void);

/**
 * @brief 刷新EPD显示（异步，非阻塞，使用当前配置的刷新模式）
 */
void lvgl_display_refresh(void);

/**
 * @brief 局部刷新EPD（仅更新变化区域，最快但可能有鬼影）
 */
void lvgl_display_refresh_partial(void);

/**
 * @brief 快速刷新EPD（全屏快速模式，平衡速度和清晰度）
 */
void lvgl_display_refresh_fast(void);

/**
 * @brief 全刷EPD（最清晰，速度最慢）
 */
void lvgl_display_refresh_full(void);

/**
 * @brief 设置刷新模式
 * @param mode 刷新模式
 */
void lvgl_set_refresh_mode(epd_refresh_mode_t mode);

/**
 * @brief 获取当前刷新模式
 * @return 当前刷新模式
 */
epd_refresh_mode_t lvgl_get_refresh_mode(void);

/**
 * @brief 检查 EPD 是否正在刷新
 * @return true 表示正在刷新，false 表示空闲
 */
bool lvgl_is_refreshing(void);

/**
 * @brief 重置刷新状态（清除脏区域标记和局部刷新计数器）
 * 在切换屏幕时调用，确保新的刷新请求不会受到旧状态影响
 */
void lvgl_reset_refresh_state(void);

/**
 * @brief 注册刷新完成回调
 * @param callback 回调函数（可选，传NULL清除）
 */
void lvgl_register_refresh_complete_callback(void (*callback)(void));

/**
 * @brief 手动触发 LVGL 渲染刷新（用于 EPD 手动刷新模式）
 *
 * 在手动刷新模式下，LVGL 不会自动调用 lv_timer_handler()
 * 需要在 UI 更新后调用此函数触发渲染
 *
 * @param disp 显示设备指针（传 NULL 使用默认显示）
 */
void lvgl_trigger_render(lv_display_t *disp);

/**
 * @brief 初始化LVGL输入设备驱动
 * @return 输入设备指针
 */
lv_indev_t* lvgl_input_init(void);

/**
 * @brief LVGL tick任务
 * @param arg 任务参数
 */
void lvgl_tick_task(void *arg);

/**
 * @brief LVGL定时器任务
 * @param arg 任务参数
 */
void lvgl_timer_task(void *arg);

#endif // LVGL_DRIVER_H
