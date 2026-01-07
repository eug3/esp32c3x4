/**
 * @file power_manager.h
 * @brief 电源与休眠管理
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>

/**
 * @brief 电源状态枚举
 */
typedef enum {
    POWER_STATE_NORMAL,       // 正常工作
    POWER_STATE_LIGHT_SLEEP,  // 轻度休眠
} power_state_t;

/**
 * @brief 进入轻度休眠：显示当前壁纸并进入 light sleep。
 * 唤醒源：GPIO（电源键）。
 */
void power_enter_light_sleep(void);

/**
 * @brief 进入深度休眠：关闭外设并进入 deep sleep。
 * 唤醒源：EXT0（电源键低电平）。
 */
void power_enter_deep_sleep(void);

/**
 * @brief 获取当前电源状态
 * @return 当前的电源状态
 */
power_state_t power_get_state(void);

/**
 * @brief 设置电源状态
 * @param state 新的电源状态
 */
void power_set_state(power_state_t state);

/**
 * @brief 退出轻度休眠，回到正常状态
 */
void power_exit_light_sleep(void);

#endif // POWER_MANAGER_H
