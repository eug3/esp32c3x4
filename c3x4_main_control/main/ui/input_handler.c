/**
 * @file input_handler.c
 * @brief 按键输入处理实现
 */

#include "input_handler.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "INPUT_HANDLER";

// ADC 配置（与 main.c 保持一致）
#define BTN_GPIO1      GPIO_NUM_1  // 4个按钮: Back, Confirm, Left, Right
#define BTN_GPIO2      GPIO_NUM_2  // 2个按钮: Volume Up, Volume Down
#define BTN_GPIO3      GPIO_NUM_3  // 电源按钮 (数字输入)

#define BTN_THRESHOLD           100    // 阈值容差
#define BTN_RIGHT_VAL           3      // Right按钮ADC值
#define BTN_LEFT_VAL            1470   // Left按钮ADC值
#define BTN_CONFIRM_VAL         2655   // Confirm按钮ADC值
#define BTN_BACK_VAL            3470   // Back按钮ADC值
#define BTN_VOLUME_DOWN_VAL     3      // Volume Down按钮ADC值
#define BTN_VOLUME_UP_VAL       2205   // Volume Up按钮ADC值

// 输入处理状态
static input_config_t s_config = {0};
static bool s_initialized = false;
static button_callback_t s_callback = NULL;
static void *s_callback_user_data = NULL;

// ADC 句柄（从外部获取）
extern adc_oneshot_unit_handle_t adc1_handle;

// 按键状态跟踪
static struct {
    button_t last_btn;
    int64_t press_time;
    int64_t last_event_time;
    bool is_pressed;
    bool is_long_pressed;
    int repeat_count;
} s_btn_state = {0};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static button_t read_button_adc(void);
static int64_t get_time_ms(void);
static void trigger_callback(button_t btn, button_event_t event);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void trigger_callback(button_t btn, button_event_t event)
{
    if (s_callback != NULL) {
        s_callback(btn, event, s_callback_user_data);
    }
}

button_t read_button_adc(void)
{
    int btn1_adc = 0, btn2_adc = 0;

    // 读取ADC值多次取平均
    for (int i = 0; i < 3; i++) {
        int adc_val;
        if (adc1_handle != NULL) {
            adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &adc_val);
            btn1_adc += adc_val;
            adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &adc_val);
            btn2_adc += adc_val;
        }
    }
    btn1_adc /= 3;
    btn2_adc /= 3;

    button_t detected_btn = BTN_NONE;

    // 检查电源按钮 (数字输入)
    if (gpio_get_level(BTN_GPIO3) == 0) {
        detected_btn = BTN_POWER;
    }
    // 检查 BTN_GPIO1 (4个按钮通过电阻分压)
    else if (btn1_adc < BTN_RIGHT_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_RIGHT;
    } else if (btn1_adc < BTN_LEFT_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_LEFT;
    } else if (btn1_adc < BTN_CONFIRM_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_CONFIRM;
    } else if (btn1_adc < BTN_BACK_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_BACK;
    }
    // 检查 BTN_GPIO2 (2个按钮通过电阻分压)
    else if (btn2_adc < BTN_VOLUME_DOWN_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_VOLUME_DOWN;
    } else if (btn2_adc < BTN_VOLUME_UP_VAL + BTN_THRESHOLD) {
        detected_btn = BTN_VOLUME_UP;
    }

    return detected_btn;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool input_handler_init(const input_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Input handler already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing input handler...");

    // 应用配置
    if (config != NULL) {
        s_config = *config;
    } else {
        // 默认配置
        s_config.enable_debounce = true;
        s_config.enable_long_press = true;
        s_config.enable_repeat = true;
        s_config.debounce_ms = BTN_DEBOUNCE_TIME;
        s_config.long_press_ms = BTN_LONG_PRESS_TIME;
        s_config.repeat_delay_ms = BTN_REPEAT_DELAY;
        s_config.repeat_interval_ms = BTN_REPEAT_INTERVAL;
    }

    // 清零按键状态
    memset(&s_btn_state, 0, sizeof(s_btn_state));

    s_initialized = true;
    ESP_LOGI(TAG, "Input handler initialized");
    return true;
}

void input_handler_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    s_callback = NULL;
    s_callback_user_data = NULL;
    s_initialized = false;

    ESP_LOGI(TAG, "Input handler deinitialized");
}

bool input_handler_register_callback(button_callback_t callback, void *user_data)
{
    s_callback = callback;
    s_callback_user_data = user_data;
    return true;
}

void input_handler_unregister_callback(void)
{
    s_callback = NULL;
    s_callback_user_data = NULL;
}

bool input_handler_get_event(button_t *btn, button_event_t *event, int timeout_ms)
{
    // TODO: 实现事件队列
    // 当前版本使用轮询模式
    return false;
}

void input_handler_poll(void)
{
    static bool first_poll = true;
    if (first_poll) {
        ESP_LOGI(TAG, "First poll - checking button state");
        first_poll = false;
    }
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Poll called but not initialized!");
        return;
    }

    button_t current_btn = read_button_adc();
    int64_t current_time = get_time_ms();
    
    // 添加调试：检测到按键时输出
    static button_t last_detected = BTN_NONE;
    if (current_btn != last_detected) {
        ESP_LOGI(TAG, "Button changed: %s -> %s", 
                 input_handler_get_button_name(last_detected),
                 input_handler_get_button_name(current_btn));
        last_detected = current_btn;
    }

    if (current_btn == BTN_NONE) {
        // 没有按键按下
        if (s_btn_state.is_pressed) {
            // 按键释放
            trigger_callback(s_btn_state.last_btn, BTN_EVENT_RELEASED);

            // 重置状态
            s_btn_state.is_pressed = false;
            s_btn_state.is_long_pressed = false;
            s_btn_state.repeat_count = 0;
            s_btn_state.last_btn = BTN_NONE;
        }
        return;
    }

    // 有按键按下
    if (!s_btn_state.is_pressed) {
        // 新按键按下
        s_btn_state.last_btn = current_btn;
        s_btn_state.is_pressed = true;
        s_btn_state.press_time = current_time;
        s_btn_state.last_event_time = current_time;

        // 防抖检查
        if (s_config.enable_debounce) {
            vTaskDelay(pdMS_TO_TICKS(s_config.debounce_ms));
            // 重新读取确认
            button_t verify_btn = read_button_adc();
            if (verify_btn != current_btn) {
                // 防抖失败，重置状态
                s_btn_state.is_pressed = false;
                s_btn_state.last_btn = BTN_NONE;
                return;
            }
        }

        // 触发按下事件
        trigger_callback(current_btn, BTN_EVENT_PRESSED);
    } else {
        // 按键持续按下
        int elapsed = current_time - s_btn_state.press_time;

        // 长按检测
        if (s_config.enable_long_press && !s_btn_state.is_long_pressed && elapsed >= s_config.long_press_ms) {
            s_btn_state.is_long_pressed = true;
            trigger_callback(current_btn, BTN_EVENT_LONG_PRESSED);
        }

        // 重复检测
        if (s_config.enable_repeat) {
            int repeat_delay = (s_btn_state.repeat_count == 0) ?
                s_config.repeat_delay_ms : s_config.repeat_interval_ms;

            if (current_time - s_btn_state.last_event_time >= repeat_delay) {
                s_btn_state.repeat_count++;
                s_btn_state.last_event_time = current_time;
                trigger_callback(current_btn, BTN_EVENT_REPEAT);
            }
        }
    }
}

const char* input_handler_get_button_name(button_t btn)
{
    switch (btn) {
        case BTN_NONE:        return "None";
        case BTN_RIGHT:       return "RIGHT";
        case BTN_LEFT:        return "LEFT";
        case BTN_CONFIRM:     return "CONFIRM";
        case BTN_BACK:        return "BACK";
        case BTN_VOLUME_UP:   return "VOLUME_UP";
        case BTN_VOLUME_DOWN: return "VOLUME_DOWN";
        case BTN_POWER:       return "POWER";
        default:              return "Unknown";
    }
}

const char* input_handler_get_event_name(button_event_t event)
{
    switch (event) {
        case BTN_EVENT_NONE:        return "NONE";
        case BTN_EVENT_PRESSED:     return "PRESSED";
        case BTN_EVENT_RELEASED:    return "RELEASED";
        case BTN_EVENT_LONG_PRESSED: return "LONG_PRESSED";
        case BTN_EVENT_REPEAT:      return "REPEAT";
        default:                    return "Unknown";
    }
}

bool input_handler_wait_for_button(button_t *btn)
{
    while (true) {
        button_t current = read_button_adc();
        if (current != BTN_NONE) {
            if (btn != NULL) {
                *btn = current;
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

button_t read_raw_button(void)
{
    return read_button_adc();
}
