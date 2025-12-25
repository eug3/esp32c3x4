/*
  display_demo.ino
  最小演示：使用已知引脚映射初始化并做最简单的 BUSY/复位 检查。

  注意：该 sketch 使用占位的 GxEPD2_43 注释。请先在 Arduino IDE/PlatformIO 中
  安装 `GxEPD2` 与 `Adafruit_GFX`，并替换为库中实际的 4.3" 模板后再编译。

  引脚映射（按固件/硬件推断）：
    SCK  = 6
    MOSI = 7
    CS   = 10
    DC   = 2
    RST  = 3
    BUSY = 4
*/

#include <Arduino.h>
#include <SPI.h>
//#include <GxEPD2_43.h> // 替换为库的实际头
// #include <GxEPD2_BW.h>

#define EPD_SCK   6
#define EPD_MOSI  7
#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  4

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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("display_demo 启动");

  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  digitalWrite(EPD_CS, HIGH);

  setup_spi_pins();
  hardwareReset();
  Serial.println("复位完成，等待 BUSY");
  waitBusy();

  Serial.println("如果已安装并配置 GxEPD2，请替换示例中的占位头文件并调用 display.init() 与演示绘制代码。\n");
}

void loop() {
  // 按回车在串口触发一次复位+BUSY 检查
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    hardwareReset();
    waitBusy();
    Serial.println("复位+BUSY 检查完成");
  }
  delay(200);
}
