/*
  GxEPD2 支持示例（ED043WC3）

  说明：
  - 这是一个示例草稿，将已知的 GPIO 引脚映射到 GxEPD2 使用的位置。
  - 已知引脚（来自固件/硬件核对）：
      SCK  = 6
      MOSI = 7
      CS   = 10
      DC   = 2
      RST  = 3
      BUSY = 4
  - 请在 `GxEPD2` 库中选择合适的驱动类（替换下面的 `GxEPD2_XXX`）。

  使用步骤：
  1. 在 Arduino IDE / PlatformIO 中安装 `GxEPD2` 库。
  2. 根据显示控制器替换 `GxEPD2_XXX`（ED043WC3 常见别名/控制器见数据手册）。
  3. 若需自定义 SPI 引脚（ESP32-C3），示例中启用了 `SPIClass spi` 并调用 `spi.begin(SCK, MISO, MOSI, CS)`。
  4. 上传并在串口观察日志；若显示未更新，先仅做复位/读 BUSY 验证逻辑。
*/

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

// ===== 引脚映射（按固件/硬件核对） =====
#define EPD_SCK   6
#define EPD_MOSI  7
#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  4

// ===== 使用 GxEPD2_43 占位（4.3" 480x800） =====
// 如果你的 GxEPD2 库包含名为 `GxEPD2_43` 或类似的 4.3" 模板，
// 你可以按如下示例取消注释并替换为库中实际的头文件/类：
//
// #include <GxEPD2_43.h> // 根据库实际头文件替换
// using EpdType = GxEPD2_43; // 如果库导出了该别名/模板
// EpdType display(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
//
// 如果库没有直接的 `GxEPD2_43`，请使用库中提供的模板形式：
// using EpdType = GxEPD2_BW<SomeTemplate, SomeTemplate::HEIGHT>;
// EpdType display(SomeTemplate(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
//
// 说明：此处仅为占位示例。替换完成后在 `setup()` 中调用
// `display.init()` 和必要的演示/更新代码以验证显示。

// 如果你不知道具体模板，在替换前可先上传以下用于验证引脚与 SPI 的最小测试。

SPIClass spi = SPI;

void setup_spi_pins()
{
  // 在 ESP32-C3 上使用自定义 SPI 引脚
  spi.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  // 可选：调整 SPI 频率
  spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
}

void hardwareReset()
{
  pinMode(EPD_RST, OUTPUT);
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  digitalWrite(EPD_RST, LOW);
  delay(5);
  digitalWrite(EPD_RST, HIGH);
  delay(20);
}

void checkBusy()
{
  pinMode(EPD_BUSY, INPUT);
  unsigned long start = millis();
  while (digitalRead(EPD_BUSY) == HIGH) {
    if (millis() - start > 5000) {
      Serial.println("BUSY 超时");
      break;
    }
    delay(10);
  }
  Serial.println("BUSY 释放或超时");
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("启动 GxEPD2 ED043WC3 示例（占位）");

  // 初始化引脚/SPI
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  digitalWrite(EPD_CS, HIGH);
  digitalWrite(EPD_DC, LOW);

  setup_spi_pins();
  hardwareReset();

  // 先验证 BUSY 引脚
  checkBusy();

  Serial.println("完成最小引脚/SPI 验证。请替换 GxEPD2 驱动模板并启用 display.init() 调用。");
}

void loop()
{
  // 等待串口输入后重置并检查 BUSY
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    hardwareReset();
    checkBusy();
    Serial.println("重置+BUSY 检查完成");
  }
  delay(200);
}
