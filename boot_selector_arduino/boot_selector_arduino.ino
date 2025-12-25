/*
 * ESP32-C3 Arduino 启动选择器
 * 支持在启动时选择 app0/app1/app2
 * 
 * 使用 Arduino IDE 编译并上传到 ESP32-C3
 */

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#define TIMEOUT_SECONDS 10
#define GPIO_BOOT_SELECT 9  // GPIO9 拉低强制进入菜单

struct AppInfo {
    const char* name;
    esp_partition_subtype_t subtype;
};

const AppInfo apps[] = {
    {"app0", ESP_PARTITION_SUBTYPE_APP_OTA_0},
    {"app1", ESP_PARTITION_SUBTYPE_APP_OTA_1},
    {"app2", ESP_PARTITION_SUBTYPE_APP_OTA_2},
};
const int numApps = sizeof(apps) / sizeof(apps[0]);

void printBootMenu() {
    Serial.println();
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║     ESP32-C3 启动选择器 v1.0          ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    Serial.println("请选择要启动的应用程序：");
    Serial.println();
    
    for (int i = 0; i < numApps; i++) {
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, 
            apps[i].subtype, 
            apps[i].name
        );
        Serial.print("  [");
        Serial.print(i);
        Serial.print("] ");
        Serial.print(apps[i].name);
        if (part) {
            Serial.print(" (已找到，大小: 0x");
            Serial.print(part->size, HEX);
            Serial.println(")");
        } else {
            Serial.println(" (未找到)");
        }
    }
    
    Serial.println();
    Serial.println("  [r] 重新显示菜单");
    Serial.println("  [s] 保存选择并设为默认");
    Serial.println();
    Serial.print("倒计时 ");
    Serial.print(TIMEOUT_SECONDS);
    Serial.println(" 秒后将启动默认应用...");
    Serial.print("请按数字键选择: ");
    Serial.flush();
}

int getDefaultAppFromNVS() {
    nvs_handle_t handle;
    int32_t defaultApp = 0;
    
    esp_err_t err = nvs_open("boot_sel", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_i32(handle, "default", &defaultApp);
        nvs_close(handle);
    }
    
    return (int)defaultApp;
}

void saveDefaultAppToNVS(int appIndex) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("boot_sel", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_i32(handle, "default", appIndex);
        nvs_commit(handle);
        nvs_close(handle);
        Serial.print("✓ 已保存 ");
        Serial.print(apps[appIndex].name);
        Serial.println(" 为默认启动应用");
    } else {
        Serial.print("✗ 保存失败: ");
        Serial.println(esp_err_to_name(err));
    }
}

void bootToApp(int appIndex, bool saveAsDefault) {
    if (appIndex < 0 || appIndex >= numApps) {
        Serial.print("✗ 无效的应用索引: ");
        Serial.println(appIndex);
        return;
    }
    
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        apps[appIndex].subtype,
        apps[appIndex].name
    );
    
    if (!partition) {
        Serial.print("✗ 未找到 ");
        Serial.print(apps[appIndex].name);
        Serial.println(" 分区");
        return;
    }
    
    if (saveAsDefault) {
        saveDefaultAppToNVS(appIndex);
    }
    
    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        Serial.print("✗ 设置启动分区失败: ");
        Serial.println(esp_err_to_name(err));
        return;
    }
    
    Serial.println();
    Serial.print("正在启动 ");
    Serial.print(apps[appIndex].name);
    Serial.println("...");
    delay(500);
    ESP.restart();
}

int checkGpioSelection() {
    pinMode(GPIO_BOOT_SELECT, INPUT_PULLUP);
    delay(10);
    
    int level = digitalRead(GPIO_BOOT_SELECT);
    
    // 如果 GPIO 为低电平（按钮按下），进入选择模式
    return (level == LOW) ? -1 : getDefaultAppFromNVS();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // 检查 GPIO 是否要求进入选择模式
    int gpioChoice = checkGpioSelection();
    bool forceMenu = (gpioChoice < 0);
    int defaultApp = forceMenu ? 0 : gpioChoice;
    
    Serial.println();
    Serial.println("启动选择器已启动");
    Serial.print("GPIO 检查: ");
    Serial.println(forceMenu ? "强制菜单" : "自动启动");
    Serial.print("默认应用: ");
    Serial.print(apps[defaultApp].name);
    Serial.print(" (索引 ");
    Serial.print(defaultApp);
    Serial.println(")");
    
    printBootMenu();
    
    int countdown = TIMEOUT_SECONDS;
    int selectedApp = defaultApp;
    bool saveDefault = false;
    bool userInteracted = false;
    
    while (countdown > 0 || userInteracted) {
        if (Serial.available()) {
            char ch = Serial.read();
            
            if (ch >= '0' && ch < '0' + numApps) {
                int choice = ch - '0';
                selectedApp = choice;
                userInteracted = true;
                Serial.println();
                Serial.print("✓ 选择了 ");
                Serial.println(apps[choice].name);
                Serial.println("按回车启动，或按 's' 保存为默认并启动");
            } else if (ch == 's' || ch == 'S') {
                saveDefault = true;
                Serial.println();
                Serial.print("将保存 ");
                Serial.print(apps[selectedApp].name);
                Serial.println(" 为默认应用");
                bootToApp(selectedApp, true);
                return;
            } else if (ch == '\r' || ch == '\n') {
                if (userInteracted) {
                    bootToApp(selectedApp, saveDefault);
                    return;
                }
            } else if (ch == 'r' || ch == 'R') {
                printBootMenu();
                countdown = TIMEOUT_SECONDS;
                userInteracted = false;
            }
        } else if (!userInteracted) {
            delay(1000);
            countdown--;
            if (countdown > 0) {
                Serial.print("\r倒计时 ");
                Serial.print(countdown);
                Serial.print(" 秒...  ");
                Serial.flush();
            }
        }
    }
    
    // 超时，启动默认应用
    Serial.println();
    Serial.println();
    Serial.print("超时，启动默认应用 ");
    Serial.println(apps[selectedApp].name);
    bootToApp(selectedApp, false);
}

void loop() {
    // 不应该到这里，因为会在 setup 中重启
    delay(1000);
}
