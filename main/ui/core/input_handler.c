/**
 * @file input_handler.c
 * @brief 按键输入处理实现 - GPIO 中断版本
 */

#include "input_handler.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "INPUT_HANDLER";

// GPIO 中断相关
#define INPUT_EVENT_QUEUE_SIZE  16
#define GPIO_ISR_DEBOUNCE_US    50000  // 中断防抖时间：50000μs = 50ms

static QueueHandle_t s_input_event_queue = NULL;
static volatile int64_t s_last_isr_time = 0;  // ISR 防抖时间戳（微秒）

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
    int64_t last_release_time;  // 上次释放时间，用于双击检测
    bool is_pressed;
    bool is_long_pressed;
    int repeat_count;
    button_t last_released_btn;  // 上次释放的按钮
} s_btn_state = {0};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static button_t read_button_adc(void);
static int64_t get_time_ms(void);
static void trigger_callback(button_t btn, button_event_t event);
static void gpio_isr_handler(void *arg);  // IRAM_ATTR 在定义处添加
static void process_button_state(void);

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

/**
 * @brief GPIO 中断服务程序
 * @note 当 GPIO1, GPIO2, GPIO3 状态变化时触发
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    // 中断级防抖
    if (now - s_last_isr_time < GPIO_ISR_DEBOUNCE_US) {
        return;
    }
    s_last_isr_time = now;

    // 发送事件到队列，唤醒处理任务
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_input_event_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
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

    // 创建事件队列
    if (s_input_event_queue == NULL) {
        s_input_event_queue = xQueueCreate(INPUT_EVENT_QUEUE_SIZE, sizeof(uint32_t));
        if (s_input_event_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create input event queue");
            return false;
        }
    }

    // 安装 GPIO ISR 服务
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE 表示服务已安装
            isr_service_installed = true;
        } else {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            return false;
        }
    }

    // 配置 GPIO1 (ADC按钮组1) 的中断 - 任意边沿触发
    gpio_config_t io_conf_btn1 = {
        .pin_bit_mask = (1ULL << BTN_GPIO1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf_btn1);
    gpio_isr_handler_add(BTN_GPIO1, gpio_isr_handler, (void *)BTN_GPIO1);

    // 配置 GPIO2 (ADC按钮组2) 的中断 - 任意边沿触发
    gpio_config_t io_conf_btn2 = {
        .pin_bit_mask = (1ULL << BTN_GPIO2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf_btn2);
    gpio_isr_handler_add(BTN_GPIO2, gpio_isr_handler, (void *)BTN_GPIO2);

    // 配置 GPIO3 (电源按钮) 的中断 - 任意边沿触发
    gpio_config_t io_conf_btn3 = {
        .pin_bit_mask = (1ULL << BTN_GPIO3),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf_btn3);
    gpio_isr_handler_add(BTN_GPIO3, gpio_isr_handler, (void *)BTN_GPIO3);

    ESP_LOGI(TAG, "GPIO interrupts configured for BTN_GPIO1, BTN_GPIO2, BTN_GPIO3");

    s_initialized = true;
    ESP_LOGI(TAG, "Input handler initialized with GPIO interrupts");
    return true;
}

void input_handler_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    // 移除 GPIO ISR 处理器
    gpio_isr_handler_remove(BTN_GPIO1);
    gpio_isr_handler_remove(BTN_GPIO2);
    gpio_isr_handler_remove(BTN_GPIO3);

    // 删除事件队列
    if (s_input_event_queue != NULL) {
        vQueueDelete(s_input_event_queue);
        s_input_event_queue = NULL;
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
    if (s_input_event_queue == NULL) {
        return false;
    }

    uint32_t gpio_num;
    TickType_t wait_ticks = (timeout_ms < 0) ? portMAX_DELAY :
                            (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(s_input_event_queue, &gpio_num, wait_ticks) == pdTRUE) {
        // 读取当前按钮状态
        button_t current = read_button_adc();
        if (btn != NULL) {
            *btn = current;
        }
        if (event != NULL) {
            *event = (current != BTN_NONE) ? BTN_EVENT_PRESSED : BTN_EVENT_RELEASED;
        }
        return true;
    }
    return false;
}

/**
 * @brief 处理按键状态 - 内部函数
 * 处理当前按键状态，检测长按和重复
 */
static void process_button_state(void)
{
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
            int64_t release_time = get_time_ms();
            button_t released_btn = s_btn_state.last_btn;

            // 双击检测：检查是否在 BTN_DOUBLE_CLICK_TIME 内第二次按下同一种按钮
            if (release_time - s_btn_state.last_release_time <= BTN_DOUBLE_CLICK_TIME &&
                released_btn == s_btn_state.last_released_btn &&
                !s_btn_state.is_long_pressed) {
                // 双击触发
                trigger_callback(released_btn, BTN_EVENT_DOUBLE_CLICK);
                // 重置双击状态，避免再次触发
                s_btn_state.last_release_time = 0;
                s_btn_state.last_released_btn = BTN_NONE;
            } else {
                // 正常释放事件
                trigger_callback(released_btn, BTN_EVENT_RELEASED);
                // 记录释放的按钮和时间
                s_btn_state.last_release_time = release_time;
                s_btn_state.last_released_btn = released_btn;
            }

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

        // 防抖检查 - 中断已经做了硬件防抖，这里做软件确认
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

void input_handler_poll(void)
{
    static bool first_poll = true;
    if (first_poll) {
        ESP_LOGI(TAG, "First poll - GPIO interrupt mode enabled");
        first_poll = false;
    }

    if (!s_initialized) {
        ESP_LOGW(TAG, "Poll called but not initialized!");
        return;
    }

    // 清空队列中所有待处理的中断事件
    uint32_t gpio_num;
    int event_count = 0;
    while (s_input_event_queue != NULL &&
           xQueueReceive(s_input_event_queue, &gpio_num, 0) == pdTRUE) {
        event_count++;
        ESP_LOGD(TAG, "GPIO interrupt on pin %lu", (unsigned long)gpio_num);
    }

    // 如果有中断事件，说明有按键活动
    if (event_count > 0) {
        ESP_LOGD(TAG, "Processed %d interrupt events", event_count);
    }

    // 处理按键状态（包括长按和重复检测）
    process_button_state();
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
        case BTN_EVENT_NONE:         return "NONE";
        case BTN_EVENT_PRESSED:      return "PRESSED";
        case BTN_EVENT_RELEASED:     return "RELEASED";
        case BTN_EVENT_LONG_PRESSED: return "LONG_PRESSED";
        case BTN_EVENT_REPEAT:       return "REPEAT";
        case BTN_EVENT_DOUBLE_CLICK: return "DOUBLE_CLICK";
        default:                     return "Unknown";
    }
}

bool input_handler_wait_for_button(button_t *btn)
{
    // 使用中断事件队列等待按键
    if (s_input_event_queue == NULL) {
        // 回退到轮询模式
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
    }

    // 等待中断事件
    uint32_t gpio_num;
    while (true) {
        // 阻塞等待中断事件，但加入超时以便检查按键状态
        if (xQueueReceive(s_input_event_queue, &gpio_num, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 有中断触发，读取按键
            button_t current = read_button_adc();
            if (current != BTN_NONE) {
                if (btn != NULL) {
                    *btn = current;
                }
                return true;
            }
        }
    }
    return false;
}

bool input_handler_wait_for_event_timeout(button_t *btn, int timeout_ms)
{
    if (s_input_event_queue == NULL) {
        return false;
    }

    int64_t start_time = get_time_ms();
    int64_t remaining = timeout_ms;

    while (remaining > 0 || timeout_ms == 0) {
        uint32_t gpio_num;
        TickType_t wait_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(remaining);

        if (xQueueReceive(s_input_event_queue, &gpio_num, wait_ticks) == pdTRUE) {
            button_t current = read_button_adc();
            if (current != BTN_NONE) {
                if (btn != NULL) {
                    *btn = current;
                }
                return true;
            }
        }

        if (timeout_ms > 0) {
            remaining = timeout_ms - (get_time_ms() - start_time);
        }
    }
    return false;
}

button_t read_raw_button(void)
{
    return read_button_adc();
}

bool input_handler_is_active(void)
{
    // 检查是否有按键按下
    return s_btn_state.is_pressed;
}
