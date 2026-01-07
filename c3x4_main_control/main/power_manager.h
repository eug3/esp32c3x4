/**
 * @file power_manager.h
 * @brief 电源与休眠管理
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>

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

#endif // POWER_MANAGER_H
