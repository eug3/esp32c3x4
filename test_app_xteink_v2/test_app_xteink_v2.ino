/*
 * XTEINK X4 墨水屏 GPIO 测试
 * 4.3英寸 480x800 黑白墨水屏驱动
 */

#include <Arduino.h>
#include <SPI.h>
#include "esp_chip_info.h"
#include "esp_flash.h"

// ===== GPIO 配置（基于 ESP32-C3 标准 SPI2 引脚） =====
#define EPD_SCK   6   // SPI 时钟
#define EPD_MOSI  7   // SPI 数据
#define EPD_CS    10  // 片选
#define EPD_DC    2   // 数据/命令选择
#define EPD_RST   3   // 复位
#define EPD_BUSY  4   // 忙状态

void testGPIO() {
  Serial.println("\n===== GPIO 连接测试 =====");
  
  // 测试CS引脚
  Serial.printf("测试 CS  (GPIO %d): ", EPD_CS);
  digitalWrite(EPD_CS, LOW);
  delay(50);
  digitalWrite(EPD_CS, HIGH);
  delay(50);
  Serial.println("✓");
  
  // 测试DC引脚
  Serial.printf("测试 DC  (GPIO %d): ", EPD_DC);
  digitalWrite(EPD_DC, HIGH);
  delay(50);
  digitalWrite(EPD_DC, LOW);
  delay(50);
  Serial.println("✓");
  
  // 测试RST引脚
  Serial.printf("测试 RST (GPIO %d): ", EPD_RST);
  digitalWrite(EPD_RST, LOW);
  delay(50);
  digitalWrite(EPD_RST, HIGH);
  delay(50);
  Serial.println("✓");
  
  // 读取BUSY引脚
  Serial.printf("读取 BUSY (GPIO %d): ", EPD_BUSY);
  int state = digitalRead(EPD_BUSY);
  Serial.println(state ? "HIGH" : "LOW");
  
  // SPI 测试
  Serial.println("\nSPI 测试:");
  Serial.printf("  SCK  (GPIO %d)\n", EPD_SCK);
  Serial.printf("  MOSI (GPIO %d)\n", EPD_MOSI);
  
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  SPI.setFrequency(4000000);
  
  digitalWrite(EPD_CS, LOW);
  SPI.transfer(0x55); // 测试数据
  digitalWrite(EPD_CS, HIGH);
  Serial.println("  ✓ SPI 传输测试完成");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║  XTEINK X4 墨水屏 GPIO 测试            ║");
  Serial.println("║  4.3\" 480x800 黑白墨水屏              ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // 打印设备信息
  Serial.println("\n--- 硬件信息 ---");
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  
  uint32_t flash_size;
  esp_flash_get_size(NULL, &flash_size);
  
  Serial.printf("芯片: ESP32-C3 Rev %d\n", chip_info.revision);
  Serial.printf("核心数: %d\n", chip_info.cores);
  Serial.printf("Flash: %d MB\n", flash_size / (1024 * 1024));
  Serial.printf("CPU: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("WiFi: %s\n", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "YES" : "NO");
  Serial.printf("BLE: %s\n", (chip_info.features & CHIP_FEATURE_BLE) ? "YES" : "NO");
  
  // 配置 GPIO
  Serial.println("\n--- 配置 GPIO ---");
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  
  digitalWrite(EPD_CS, HIGH);
  digitalWrite(EPD_DC, LOW);
  digitalWrite(EPD_RST, HIGH);
  
  Serial.println("✓ GPIO 初始化完成");
  
  // 执行 GPIO 测试
  testGPIO();
  
  Serial.println("\n========================================");
  Serial.println("  GPIO 测试完成");
  Serial.println("========================================");
  Serial.println("\n如果所有测试都通过，说明引脚配置正确");
  Serial.println("下一步：提取墨水屏初始化序列\n");
  Serial.println("发送任意字符重新测试 GPIO");
}

void loop() {
  // 检查串口输入
  if (Serial.available() > 0) {
    while (Serial.available()) Serial.read();  // 清空输入
    testGPIO();
  }
  
  delay(100);
}
