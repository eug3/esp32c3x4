/**
 * @file pngdec_config.h
 * @brief PNGdec ESP-IDF 适配配置
 */

#ifndef PNGDEC_CONFIG_H
#define PNGDEC_CONFIG_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ESP-IDF 平台适配
#define memcpy_P memcpy
#define PROGMEM

// 禁用 Arduino 相关代码
#define __ESP_IDF__

// 缓冲区大小 (适合 ESP32-C3 的 400KB RAM)
#define PNG_FILE_BUF_SIZE 2048

// 最大缓冲像素数 (屏幕宽度 300 像素 * 4 字节/像素 * 2 行)
#define PNG_MAX_BUFFERED_PIXELS ((300*4 + 1)*2)

#endif // PNGDEC_CONFIG_H
