/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface for ESP32
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   ESP32 V1.0
* | Date        :   2024-12-26
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

/**
 * GPIO config - Xteink X4
**/
#define EPD_RST_PIN     5
#define EPD_DC_PIN      4
#define EPD_CS_PIN      21
#define EPD_BUSY_PIN    6
#define EPD_PWR_PIN     -1  // Not used
#define EPD_MOSI_PIN    10
#define EPD_SCLK_PIN    8
#define EPD_MISO_PIN    7   // SD card data output

/*------------------------------------------------------------------------------------------------------*/
void DEV_Digital_Write(UWORD Pin, UBYTE Value);
UBYTE DEV_Digital_Read(UWORD Pin);

void DEV_SPI_WriteByte(UBYTE Value);
void DEV_SPI_Write_nByte(uint8_t *pData, uint32_t Len);
void DEV_Delay_ms(UDOUBLE xms);

UBYTE DEV_Module_Init(void);
void DEV_Module_Exit(void);

#endif