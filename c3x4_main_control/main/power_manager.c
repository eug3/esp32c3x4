/**
 * @file power_manager.c
 * @brief 电源与休眠管理实现
 */

#include "power_manager.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "wallpaper_manager.h"
#include "EPD_4in26.h"

// 与主工程保持一致的电源键定义
#ifndef BTN_GPIO3
#include "driver/gpio.h"
#define BTN_GPIO3 GPIO_NUM_3
#endif

static const char *TAG = "POWER_MGR";

// 全局电源状态
static power_state_t s_power_state = POWER_STATE_NORMAL;

power_state_t power_get_state(void)
{
    return s_power_state;
}

void power_set_state(power_state_t state)
{
    s_power_state = state;
    ESP_LOGI(TAG, "Power state changed to: %d", (int)state);
}

void power_exit_light_sleep(void)
{
    ESP_LOGI(TAG, "Exiting light sleep...");
    s_power_state = POWER_STATE_NORMAL;
}

void power_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "Entering light sleep mode (show wallpaper, continue running)...");
    s_power_state = POWER_STATE_LIGHT_SLEEP;

    // 显示当前壁纸（若未选择则清屏）
    wallpaper_show();

    // 注意：这里不调用 esp_light_sleep_start()，因为那会暂停所有代码执行，
    // 包括按键轮询循环，导致无法检测双击事件。
    // 我们只是切换到"壁纸显示状态"，主循环继续运行以响应按键。
    
    ESP_LOGI(TAG, "Light sleep mode active (wallpaper shown, waiting for double-click)");
}

void power_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Preparing deep sleep (wake on power key)...");

    // 让墨水屏进入休眠以降低功耗
    EPD_4in26_Sleep();

    // 配置 GPIO 唤醒（深度休眠）：电源键为低电平时唤醒
    gpio_wakeup_enable(BTN_GPIO3, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGI(TAG, "Entering deep sleep now...");
    esp_deep_sleep_start();
}
