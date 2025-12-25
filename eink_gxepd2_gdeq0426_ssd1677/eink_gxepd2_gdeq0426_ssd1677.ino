/*
  GDEQ0426T82 (4.26", 800x480) 示例（SSD1677 控制器）

  说明：
  - 控制器: SSD1677
  - 分辨率: 800 x 480 (宽 x 高)
  - 这是一个 GxEPD2 占位示例，包含常见绘制流程和引脚映射。
  - 你需要在 Arduino/PlatformIO 中安装 `GxEPD2` 与 `Adafruit_GFX`，并将下面的
    `GxEPD2_800x480` 占位模板替换为库中对应的实际模板名称。

  已知/推断引脚（来自固件/硬件核对）：
    SCK  = 6
    MOSI = 7
    CS   = 10
    DC   = 2
    RST  = 3
    BUSY = 4

  使用：替换模板 -> 编译 -> 上传 -> 观察串口/显示。
*/

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
// 使用库中针对 GDEQ0426T82 的驱动头
#include <gdeq/GxEPD2_426_GDEQ0426T82.h>

// 引脚定义
#define EPD_SCK   6
#define EPD_MOSI  7
#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  4

// 使用 GxEPD2 库中提供的 GDEQ0426T82 模板并实例化
using EpdType = GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT>;
EpdType display(GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

SPIClass spi = SPI;

void setup_spi_pins()
{
  spi.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
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

void waitBusy(unsigned long timeout_ms = 5000)
{
  pinMode(EPD_BUSY, INPUT);
  unsigned long start = millis();
  while (digitalRead(EPD_BUSY) == HIGH) {
    if (millis() - start > timeout_ms) {
      Serial.println("BUSY 超时");
      break;
    }
    delay(10);
  }
}

void demoDraw()
{
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(20, 40);
    display.println("Hello GDEQ0426T82");
    display.setCursor(20, 80);
    display.println("SSD1677 800x480 demo");
    display.drawRect(10, 20, 460, 100, GxEPD_BLACK);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("GDEQ0426T82 (SSD1677) 示例启动");

  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  digitalWrite(EPD_CS, HIGH);

  setup_spi_pins();
  hardwareReset();
  waitBusy();
  // 初始化显示并立即绘制测试内容
  display.init();
  delay(20);
  demoDraw();

  Serial.println("演示绘制已执行，请检查墨水屏。");
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    Serial.println("触发演示绘制（占位，需要已实例化 display）");
    demoDraw();
  }
  delay(200);
}
