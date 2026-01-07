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

void power_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "Entering light sleep (show wallpaper, wake on power key)...");

    // 显示当前壁纸（若未选择则清屏）
    wallpaper_show();

    // 配置 GPIO 唤醒（ESP-IDF: light sleep 走 gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup）
    // NOTE: 由于其他按键采用 ADC 电阻分压，无法作为 GPIO 唤醒源，实际仅电源键可唤醒。
    gpio_wakeup_enable(BTN_GPIO3, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // 进入轻度休眠
    esp_light_sleep_start();

    // 唤醒后，恢复必要外设（ePaper 保持画面，无需立即全刷）
    ESP_LOGI(TAG, "Woke from light sleep");
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
