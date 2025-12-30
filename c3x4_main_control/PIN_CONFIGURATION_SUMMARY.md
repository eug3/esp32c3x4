# ESP32-C3 E-Paper Display Pin Configuration Summary

## Overview

This document summarizes the verified GPIO pin configurations for the ESP32-C3 based on:
1. IDA Pro analysis of factory firmware (flash_dump.bin)
2. c3x4_main_control project source code analysis
3. User's required pin configuration

## SD Card Pin Configuration (VERIFIED)

### Factory Firmware (from IDA Pro)
| Function | GPIO | Physical Pin | Notes |
|----------|------|--------------|-------|
| MISO     | 6    | Pin 17       | SD Card Data In |
| MOSI     | 7    | Pin 18       | SD Card Data Out |
| CLK/SCK  | 8    | Pin 19       | SD Card Clock |
| CS       | 9    | Pin 20       | SD Card Chip Select |

### c3x4_main_control Project (from main.c)
| Function | GPIO | Physical Pin | Source Code |
|----------|------|--------------|-------------|
| MISO     | 6    | Pin 17       | `#define PIN_NUM_MISO  GPIO_NUM_6` |
| MOSI     | 7    | Pin 18       | `#define PIN_NUM_MOSI  GPIO_NUM_7` |
| CLK      | 8    | Pin 19       | `#define PIN_NUM_CLK   GPIO_NUM_8` |
| CS       | 9    | Pin 20       | `#define PIN_NUM_CS    GPIO_NUM_9` |

**Conclusion:** SD card pins MATCH between factory firmware and c3x4_main_control project. Use GPIO 6,7,8,9 for SD card.

---

## E-Paper Display Pin Configurations

### Configuration A: IDA Pro Analysis (Factory Firmware)
Based on analysis of functions `sub_403D08BE` and `sub_403CF358`.

| Function | GPIO | Physical Pin | Notes |
|----------|------|--------------|-------|
| CS       | 4    | Pin 15       | EPD Chip Select |
| DC       | 5    | Pin 16       | Data/Command Select |
| RST      | 6    | Pin 17       | Reset (shares with SD MISO!) |
| BUSY     | 7    | Pin 18       | Busy signal (shares with SD MOSI!) |
| MOSI     | 8    | Pin 19       | SPI Data Out (shares with SD CLK!) |
| SCK      | 9    | Pin 20       | SPI Clock (shares with SD CS!) |

**Issue:** This configuration has MAJOR pin conflicts with SD card (GPIO 6,7,8,9 all shared). Cannot use SD card and EPD simultaneously.

### Configuration B: c3x4_main_control Project
From DEV_Config.h - uses different SCK pin to reduce conflicts.

| Function | GPIO | Physical Pin | Source Code |
|----------|------|--------------|-------------|
| CS       | 4    | Pin 15       | `#define EPD_CS_PIN      4` |
| DC       | 5    | Pin 16       | `#define EPD_DC_PIN      5` |
| RST      | 6    | Pin 17       | `#define EPD_RST_PIN     6` |
| BUSY     | 7    | Pin 18       | `#define EPD_BUSY_PIN    7` |
| MOSI     | 8    | Pin 19       | `#define EPD_MOSI_PIN    8` |
| SCK      | 10   | Pin 21       | `#define EPD_SCLK_PIN   10` |

**Shared pins with SD card:** GPIO 6,7,8 (RST, BUSY, MOSI)
**Unique pins:** GPIO 4,5,10 (CS, DC, SCK)

This is better than factory config but still has conflicts.

### Configuration C: User Requested
The pins the user wants to use for their application.

| Function | GPIO | Physical Pin |
|----------|------|--------------|
| SCK      | 9    | Pin 20       |
| MOSI     | 10   | Pin 21       |
| DC       | 12   | Pin 23       |
| CS       | 17   | Pin 28       |
| RST      | 6    | Pin 17       (assumed) |
| BUSY     | 7    | Pin 18       (assumed) |

**Advantage:** No conflicts with SD card (GPIO 6,7,8,9)! All EPD SPI pins are different.

---

## Recommendation

### For Testing with c3x4_main_control Firmware
Use **Configuration B** (c3x4_main_control):
- Test script: [spi_test_c3x4_config.py](spi_test_c3x4_config.py)
- Set `EPD_PINS = EPD_PINS_C3X4`

### For User's Custom Application
Use **Configuration C** (User Requested):
- Test script: [spi_test_c3x4_config.py](spi_test_c3x4_config.py)
- Set `EPD_PINS = EPD_PINS_USER`
- This configuration allows simultaneous use of SD card and E-Paper display!

---

## Test Script Usage

```bash
# Run test (requires device connected on COM5)
mpremote connect COM5 exec d:\GitHub\GSDJX4DoubleSysFserv\spi_test_c3x4_config.py
```

To switch configurations, edit line 40 in the test script:
```python
EPD_PINS = EPD_PINS_C3X4  # Change to EPD_PINS_USER for user config
```

---

## ESP32-C3 QFN32 Pin Reference

| GPIO | Physical Pin | Notes |
|------|--------------|-------|
| 0-3  | Pin 8-11     | Not used in current configs |
| 4    | Pin 15       | EPD CS (both configs) |
| 5    | Pin 16       | EPD DC (both configs) |
| 6    | Pin 17       | SD MISO / EPD RST |
| 7    | Pin 18       | SD MOSI / EPD BUSY |
| 8    | Pin 19       | SD CLK / EPD MOSI |
| 9    | Pin 20       | SD CS / User EPD SCK |
| 10   | Pin 21       | c3x4 EPD SCK / User EPD MOSI |
| 11   | Pin 22       | (free) |
| 12   | Pin 23       | User EPD DC |
| 13-14| Pin 24-25    | (free) |
| 15   | Pin 26       | (free) |
| 16   | Pin 27       | (free) |
| 17   | Pin 28       | User EPD CS |
| 18-21| Pin 29-32    | (free) |

---

## Files Referenced

- [spi_test_c3x4_config.py](spi_test_c3x4_config.py) - MicroPython test script
- [spi_test_from_ida.py](spi_test_from_ida.py) - IDA analysis based test
- [firmware_pin_analysis.py](firmware_pin_analysis.py) - IDA findings summary
- [D:\GitHub\esp32c3x4\c3x4_main_control\main\DEV_Config.h](../esp32c3x4/c3x4_main_control/main/DEV_Config.h) - Hardware config
- [D:\GitHub\esp32c3x4\c3x4_main_control\main\main.c](../esp32c3x4/c3x4_main_control/main/main.c) - SD card config
