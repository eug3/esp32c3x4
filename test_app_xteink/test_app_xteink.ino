/*
 * XTEINK X4 自定义测试应用
 * 目标：验证自定义固件可以在 app1 分区正常启动
 * 
 * 功能：
 * - 显示设备信息
 * - 显示当前运行分区
 * - 自动设置 app1 为默认启动分区
 * - 心跳输出
 */

#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

void setup() {
  Serial.begin(115200);
  delay(2000);  // 等待串口稳定
  
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("  XTEINK X4 自定义应用");
  Serial.println("========================================");
  Serial.println();
  
  // 显示芯片信息
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  
  uint32_t flash_size;
  esp_flash_get_size(NULL, &flash_size);
  
  Serial.println("硬件信息:");
  Serial.printf("  型号: %s\n", CONFIG_IDF_TARGET);
  Serial.printf("  核心数: %d\n", chip_info.cores);
  Serial.printf("  Flash: %d MB %s\n", 
                flash_size / (1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
  Serial.printf("  WiFi: %s\n", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "YES" : "NO");
  Serial.printf("  BLE: %s\n", (chip_info.features & CHIP_FEATURE_BLE) ? "YES" : "NO");
  Serial.println();
  
  // 显示分区信息
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  
  Serial.println("分区信息:");
  Serial.printf("  运行分区: %s\n", running->label);
  Serial.printf("  地址: 0x%08X\n", running->address);
  Serial.printf("  大小: %d KB\n", running->size / 1024);
  Serial.printf("  启动分区: %s\n", boot->label);
  Serial.println();
  
  // 如果当前在 app1，确保设置为默认启动分区
  if (running->address == 0x650000) {
    Serial.println("检测到在 app1 运行，设置为默认启动分区...");
    
    const esp_partition_t *app1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, 
        ESP_PARTITION_SUBTYPE_APP_OTA_1, 
        NULL
    );
    
    if (app1 && esp_ota_set_boot_partition(app1) == ESP_OK) {
      Serial.println("✓ 已设置 app1 为默认启动分区");
    } else {
      Serial.println("✗ 设置失败");
    }
  } else {
    Serial.printf("⚠ 警告：当前在 0x%08X 运行，不是预期的 app1 (0x650000)\n", running->address);
  }
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("  应用启动成功！");
  Serial.println("========================================");
  Serial.println();
  Serial.println("开始心跳输出...");
}

unsigned long lastBeat = 0;
int beatCount = 0;

void loop() {
  unsigned long now = millis();
  
  if (now - lastBeat >= 1000) {
    beatCount++;
    Serial.printf("[%d] 心跳 - 运行时间: %lu 秒\n", beatCount, now / 1000);
    lastBeat = now;
  }
  
  // 每 10 秒显示一次内存信息
  if (beatCount > 0 && beatCount % 10 == 0) {
    Serial.printf("   可用堆内存: %d bytes\n", ESP.getFreeHeap());
    beatCount++;  // 避免重复显示
  }
}
