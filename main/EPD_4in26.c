/*****************************************************************************
* | File      	:  	EPD_4in26.c
* | Author      :   Waveshare team
* | Function    :   4.26inch e-paper test demo
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2023-12-19
* | Info        :
* -----------------------------------------------------------------------------
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "EPD_4in26.h"
#include "Debug.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

// Waveform LUT data for different temperature ranges (from GDEQ0426T82 reference)
// 0--5 degrees Celsius
static const unsigned char WS_0_5[112] = {
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1E, 0x23, 0x21, 0x23, 0x00,
	0x28, 0x01, 0x28, 0x01, 0x03,
	0x1B, 0x19, 0x05, 0x03, 0x01,
	0x05, 0x00, 0x08, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x48,
	0x00, 0x00,
};

// 5--10 degrees Celsius
static const unsigned char WS_5_10[112] = {
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1E, 0x23, 0x05, 0x02, 0x00,
	0x2B, 0x01, 0x2B, 0x01, 0x02,
	0x1B, 0x19, 0x05, 0x03, 0x00,
	0x05, 0x00, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x48,
	0x00, 0x00,
};

// 10--15 degrees Celsius
static const unsigned char WS_10_15[112] = {
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xAA, 0x48, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x48, 0xAA, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x14, 0x1A, 0x0B, 0x06, 0x00,
	0x21, 0x01, 0x21, 0x01, 0x02,
	0x18, 0x16, 0x05, 0x03, 0x00,
	0x04, 0x00, 0x05, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x48,
	0x00, 0x00,
};

// 15--20 degrees Celsius
static const unsigned char WS_15_20[112] = {
	0xA2, 0x48, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x54, 0x48, 0xA8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xA2, 0x48, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x54, 0x48, 0xA8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0D, 0x0D, 0x08, 0x05, 0x00,
	0x0F, 0x01, 0x0F, 0x01, 0x04,
	0x0D, 0x0D, 0x05, 0x05, 0x00,
	0x03, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x48,
	0x00, 0x00,
};

// 20--80 degrees Celsius
static const unsigned char WS_20_80[112] = {
	0xA0, 0x48, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x50, 0x48, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xA0, 0x48, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x50, 0x48, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1A, 0x14, 0x00, 0x00, 0x00,
	0x0D, 0x01, 0x0D, 0x01, 0x02,
	0x0A, 0x0A, 0x03, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x48,
	0x00, 0x00,
};

// 80--127 degrees Celsius (Fast refresh mode)
static const unsigned char WS_80_127[112] = {
	0xA8, 0x00, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x54, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xA8, 0x00, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x54, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0C, 0x0D, 0x0B, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x0A, 0x0A, 0x05, 0x0B, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x01,
	0x22, 0x22, 0x22, 0x22, 0x22,
	0x17, 0x41, 0xA8, 0x32, 0x30,
	0x00, 0x00,
};	

/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
static void EPD_4in26_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(100);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(100);
}

/* Forward declarations for functions used before their definition */
static void EPD_4in26_SendCommand(UBYTE Reg);
static void EPD_4in26_SendData(UBYTE Data);
void EPD_4in26_ReadBusy(void);
static void EPD_4in26_WriteLUT_TemperatureCompensated(void);
static void EPD_4in26_WriteLUT_Fast(void);

/******************************************************************************
function :	Clear RAM using command 0x46 and 0x47 (as per initialization flowchart)
parameter:
******************************************************************************/
static void EPD_4in26_ClearRAM(void)
{
	// Step 3 in flowchart: Clear and fill two RAM by Command 0x46, Data 0xF7
	EPD_4in26_SendCommand(0x46);
	EPD_4in26_SendData(0xF7);
	EPD_4in26_ReadBusy();
	
	// Fill RAM 0x26 by Command 0x47, Data 0xF7
	EPD_4in26_SendCommand(0x47);
	EPD_4in26_SendData(0xF7);
	EPD_4in26_ReadBusy();
}

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_4in26_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_4in26_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_4in26_SendData2(UBYTE *pData, UDOUBLE len)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_Write_nByte(pData, len);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_4in26_SendRegion_FromFramebuffer(UBYTE *full_framebuffer,
									 uint32_t fb_stride,
									 UWORD x_offset_bytes,
									 UWORD w_bytes,
									 UWORD y,
									 UWORD h_actual)
{
	// 优先尝试“整块发送”：
	// - 若窗口是全宽字节（x=0 且 w_bytes==stride），则 framebuffer 中该区域是连续内存，可直接一次性发送。
	// - 否则，尝试分配临时缓冲，把每行的窗口数据拼成连续内存，再一次性发送。
	// - 若 malloc 失败，则回退到逐行发送（当前的流式方式）。

	if (x_offset_bytes == 0 && w_bytes == fb_stride) {
		UBYTE *base = full_framebuffer + (uint32_t)y * fb_stride;
		const UDOUBLE len = (UDOUBLE)((uint32_t)h_actual * fb_stride);
		EPD_4in26_SendData2(base, len);
		return;
	}

	const size_t total_bytes = (size_t)w_bytes * (size_t)h_actual;
	UBYTE *block = (UBYTE *)malloc(total_bytes);
	if (block) {
		for (UWORD i = 0; i < h_actual; i++) {
			UBYTE *row_ptr = full_framebuffer + (uint32_t)(y + i) * fb_stride + x_offset_bytes;
			memcpy(block + (size_t)i * w_bytes, row_ptr, w_bytes);
		}
		EPD_4in26_SendData2(block, (UDOUBLE)total_bytes);
		free(block);
		return;
	}

	// fallback: row-by-row
	for (UWORD i = 0; i < h_actual; i++) {
		UBYTE *row_ptr = full_framebuffer + (uint32_t)(y + i) * fb_stride + x_offset_bytes;
		EPD_4in26_SendData2(row_ptr, w_bytes);
	}
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
void EPD_4in26_ReadBusy(void)
{
	int timeout_ms = 5000;  // 5秒超时
	int elapsed = 0;

	while(1)
	{	 //=1 BUSY
		if(DEV_Digital_Read(EPD_BUSY_PIN)==0) {
			// BUSY 变低，刷新完成
			break;
		}
		// 优化：减少延时，从 20ms 降低到 5ms，提高响应速度
		DEV_Delay_ms(20);
		elapsed += 20;

		if (elapsed >= timeout_ms) {
			ESP_LOGW("EPD", "BUSY timeout after %d ms, BUSY pin still high!", timeout_ms);
			break;
		}

		// 每 500ms 输出一次日志（避免刷屏）
		if (elapsed % 500 == 0 && elapsed < timeout_ms) {
			ESP_LOGW("EPD", "Waiting for BUSY... elapsed=%d ms", elapsed);
		}
	}
	DEV_Delay_ms(20);
	// 移除额外的延时，刷新完成后立即返回
}

/******************************************************************************
function :	Turn On Display
parameter:
******************************************************************************/
static void EPD_4in26_TurnOnDisplay(void)
{
	EPD_4in26_SendCommand(0x22); //Display Update Control
	EPD_4in26_SendData(0xF7);
	EPD_4in26_SendCommand(0x20); //Activate Display Update Sequence
	EPD_4in26_ReadBusy();
}

static void EPD_4in26_TurnOnDisplay_Fast(void)
{
	EPD_4in26_SendCommand(0x22); //Display Update Control
	EPD_4in26_SendData(0xFC);
	EPD_4in26_SendCommand(0x20); //Activate Display Update Sequence
	EPD_4in26_ReadBusy();
}

static void EPD_4in26_TurnOnDisplay_Part(void)
{
	// 参考 Arduino EPD_Part_Update
	// 1. 加载温度补偿的波形 LUT
	EPD_4in26_WriteLUT_TemperatureCompensated();

	// 2. 执行局部刷新
	EPD_4in26_SendCommand(0x22); // Display Update Control
	EPD_4in26_SendData(0xFC);    // partial update mode
	EPD_4in26_SendCommand(0x20); // Master Activation
	EPD_4in26_ReadBusy();
}

/******************************************************************************
function :	Read EPD temperature sensor
parameter:	Return temperature in degrees Celsius
******************************************************************************/
static int EPD_4in26_ReadTemperature(void)
{
	int tempvalue;
	UBYTE temp1, temp2;

	// Enable temperature reading
	EPD_4in26_SendCommand(0x18);
	EPD_4in26_SendData(0x80);

	// Trigger temperature measurement
	EPD_4in26_SendCommand(0x22);
	EPD_4in26_SendData(0xB1);
	EPD_4in26_SendCommand(0x20);
	EPD_4in26_ReadBusy();

	// Read temperature data
	EPD_4in26_SendCommand(0x1B);
	EPD_4in26_SendData(0x00);  // Send dummy byte to clock in data
	temp1 = DEV_SPI_ReadByte();  // D11-D4
	temp2 = DEV_SPI_ReadByte();  // D3-D0 (with padding)

	// Combine temperature value (12-bit signed)
	tempvalue = (temp1 << 8) | temp2;
	tempvalue = tempvalue >> 4;  // Right align to 12 bits

	// Convert to temperature (signed 12-bit value)
	// D11 is sign bit
	if (tempvalue & 0x800) {
		// Negative temperature
		tempvalue = tempvalue - 0x1000;
	}

	// Temperature is in 1/16 degree units
	tempvalue = tempvalue / 16;

	ESP_LOGI("EPD_TEMP", "Temperature: %d°C", tempvalue);
	return tempvalue;
}

/******************************************************************************
function :	Write waveform LUT to EPD
parameter:
	waveform: Pointer to waveform LUT data (112 bytes)
******************************************************************************/
static void EPD_4in26_WriteLUT(const unsigned char *waveform)
{
	UBYTE i;

	// Write LUT to registers 0x32
	EPD_4in26_SendCommand(0x32); // Write LUT register
	for (i = 0; i < 105; i++) {
		EPD_4in26_SendData(*waveform++);
	}
	EPD_4in26_ReadBusy();

	// Write remaining waveform parameters
	EPD_4in26_SendCommand(0x03); // VGH
	EPD_4in26_SendData(*waveform++);

	EPD_4in26_SendCommand(0x04); // VSH1, VSH2, VSL
	EPD_4in26_SendData(*waveform++);
	EPD_4in26_SendData(*waveform++);
	EPD_4in26_SendData(*waveform++);

	EPD_4in26_SendCommand(0x2C); // VCOM voltage
	EPD_4in26_SendData(*waveform++);
}

/******************************************************************************
function :	Write temperature-compensated waveform LUT
parameter:	Automatically selects LUT based on current temperature
******************************************************************************/
static void EPD_4in26_WriteLUT_TemperatureCompensated(void)
{
	float temp;
	const unsigned char *lut_to_use = NULL;

	temp = (float)EPD_4in26_ReadTemperature();

	if (temp <= 5) {
		lut_to_use = WS_0_5;
		ESP_LOGI("EPD_LUT", "Using LUT for temperature range: 0-5°C");
	} else if (temp <= 10) {
		lut_to_use = WS_5_10;
		ESP_LOGI("EPD_LUT", "Using LUT for temperature range: 5-10°C");
	} else if (temp <= 15) {
		lut_to_use = WS_10_15;
		ESP_LOGI("EPD_LUT", "Using LUT for temperature range: 10-15°C");
	} else if (temp <= 20) {
		lut_to_use = WS_15_20;
		ESP_LOGI("EPD_LUT", "Using LUT for temperature range: 15-20°C");
	} else {
		lut_to_use = WS_20_80;
		ESP_LOGI("EPD_LUT", "Using LUT for temperature range: 20-80°C");
	}

	if (lut_to_use != NULL) {
		EPD_4in26_WriteLUT(lut_to_use);
	}
}

/******************************************************************************
function :	Write fast refresh waveform LUT
parameter:	Uses WS_80_127 LUT for fast partial refresh
******************************************************************************/
static void EPD_4in26_WriteLUT_Fast(void)
{
	ESP_LOGI("EPD_LUT", "Using fast refresh LUT");
	EPD_4in26_WriteLUT(WS_80_127);
}

/******************************************************************************
function :	Setting the display window
parameter:
******************************************************************************/
static void EPD_4in26_SetWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    ESP_LOGI("EPD_WIN", "SetWindows: X=[%u,%u], Y=[%u,%u]", Xstart, Xend, Ystart, Yend);
    
    EPD_4in26_SendCommand(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
    EPD_4in26_SendData(Xstart & 0xFF);
    EPD_4in26_SendData((Xstart>>8) & 0x03);
    EPD_4in26_SendData(Xend & 0xFF);
    EPD_4in26_SendData((Xend>>8) & 0x03);
	
    EPD_4in26_SendCommand(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
    EPD_4in26_SendData(Ystart & 0xFF);
    EPD_4in26_SendData((Ystart>>8) & 0x03);
    EPD_4in26_SendData(Yend & 0xFF);
    EPD_4in26_SendData((Yend>>8) & 0x03);
}

/******************************************************************************
function :	Set Cursor
parameter:
******************************************************************************/
static void EPD_4in26_SetCursor(UWORD Xstart, UWORD Ystart)
{
    ESP_LOGI("EPD_CUR", "SetCursor: X=%u, Y=%u", Xstart, Ystart);
    
    EPD_4in26_SendCommand(0x4E); // SET_RAM_X_ADDRESS_COUNTER
    EPD_4in26_SendData(Xstart & 0xFF);
    EPD_4in26_SendData((Xstart>>8) & 0x03);

    EPD_4in26_SendCommand(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
    EPD_4in26_SendData(Ystart & 0xFF);
    EPD_4in26_SendData((Ystart>>8) & 0x03);
}

/******************************************************************************
function :	Initialize the e-Paper register (follows flowchart strictly)
parameter:
******************************************************************************/
void EPD_4in26_Init(void)
{
	// Step 1: Power On (handled by hardware/VCI supply)
	// Step 2: Set Initial Configuration
	EPD_4in26_Reset();  // HW Reset
	DEV_Delay_ms(10);   // Wait 10ms as per flowchart

	EPD_4in26_ReadBusy();   
	EPD_4in26_SendCommand(0x12);  // SW Reset by Command 0x12
	EPD_4in26_ReadBusy();   
	DEV_Delay_ms(10);   // Wait 10ms as per flowchart
	
	// Step 3: Send Initialization Code
	// Clear and fill RAM using 0x46/0x47
	EPD_4in26_ClearRAM();
	
	// Set gate driver output by Command 0x01
	EPD_4in26_SendCommand(0x01);   // Drive output control    
	EPD_4in26_SendData((EPD_4in26_HEIGHT-1)%256); //  Y  
	EPD_4in26_SendData((EPD_4in26_HEIGHT-1)/256); //  Y 
	EPD_4in26_SendData(0x02);

	// Set display RAM size by Command 0x11, 0x44, 0x45
	EPD_4in26_SendCommand(0x11);        // Data entry mode
	EPD_4in26_SendData(0x01);           // X-mode x+ y-

	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);

	// Set panel border by Command 0x3C
	EPD_4in26_SendCommand(0x3C);        // Border setting 
	EPD_4in26_SendData(0x01);

	// Soft start setting
	EPD_4in26_SendCommand(0x0C);        // Soft start     
	EPD_4in26_SendData(0xAE);
	EPD_4in26_SendData(0xC7);
	EPD_4in26_SendData(0xC3);
	EPD_4in26_SendData(0xC0);
	EPD_4in26_SendData(0x80);

	// Step 4: Load Waveform LUT
	// Sense temperature by mVext TS by Command 0x18
	EPD_4in26_SendCommand(0x18); // Use internal temperature sensor
	EPD_4in26_SendData(0x80);

	// Load waveform LUT from OTP by Command 0x22, 0x20 to MCU
	EPD_4in26_SendCommand(0x22);  // Display Update Control
	EPD_4in26_SendData(0xB1);     // Load LUT from OTP
	EPD_4in26_SendCommand(0x20);  // Activate Display Update Sequence
	EPD_4in26_ReadBusy();

	EPD_4in26_SetCursor(0, 0);
}

void EPD_4in26_Init_Fast(void)
{
	EPD_4in26_Reset();
	DEV_Delay_ms(100);

	EPD_4in26_ReadBusy();   
	EPD_4in26_SendCommand(0x12);  //SWRESET
	EPD_4in26_ReadBusy();   
	
	EPD_4in26_SendCommand(0x18); // use the internal temperature sensor
	EPD_4in26_SendData(0x80);

	EPD_4in26_SendCommand(0x0C); //set soft start     
	EPD_4in26_SendData(0xAE);
	EPD_4in26_SendData(0xC7);
	EPD_4in26_SendData(0xC3);
	EPD_4in26_SendData(0xC0);
	EPD_4in26_SendData(0x80);

	EPD_4in26_SendCommand(0x01);   //      drive output control    
	EPD_4in26_SendData((EPD_4in26_HEIGHT-1)%256); //  Y  
	EPD_4in26_SendData((EPD_4in26_HEIGHT-1)/256); //  Y 
	EPD_4in26_SendData(0x02);

	EPD_4in26_SendCommand(0x3C);        // Border       Border setting 
	EPD_4in26_SendData(0x01);

	EPD_4in26_SendCommand(0x11);        //    data  entry  mode
	EPD_4in26_SendData(0x01);           //       X-mode  x+ y- (original)

	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);

	EPD_4in26_SetCursor(0, 0);

	// 设置快刷模式的温度补偿（只需设置一次，后续刷新保持）
	EPD_4in26_SendCommand(0x1A);
	EPD_4in26_SendData(0x5A);

	// 加载波形 LUT（从 OTP）
	EPD_4in26_SendCommand(0x22);  // Display Update Control
	EPD_4in26_SendData(0xB1);     // Load LUT from OTP
	EPD_4in26_SendCommand(0x20);  // Activate Display Update Sequence
	EPD_4in26_ReadBusy();
}

/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
void EPD_4in26_Clear(void)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;
	UBYTE image[EPD_4in26_WIDTH / 8] = {0x00};
    for(i=0; i<width; i++) {
        image[i] = 0xff;
    }

	// 重新设置全屏窗口
	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);
	EPD_4in26_SetCursor(0, 0);

	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
	    EPD_4in26_SendData2(image, width);
	}

	EPD_4in26_SendCommand(0x26);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
		EPD_4in26_SendData2(image, width);
	}
	EPD_4in26_TurnOnDisplay();
}

/******************************************************************************
function :	Clear screen using fast refresh mode
parameter:
******************************************************************************/
void EPD_4in26_Clear_Fast(void)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;
	UBYTE image[EPD_4in26_WIDTH / 8] = {0x00};
    for(i=0; i<width; i++) {
        image[i] = 0xff;
    }

	// 重新设置全屏窗口
	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);
	EPD_4in26_SetCursor(0, 0);

	// 设置快刷模式的温度补偿
	EPD_4in26_SendCommand(0x1A);
	EPD_4in26_SendData(0x5A);

	// 写入当前图像缓冲区 (0x24)
	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
	    EPD_4in26_SendData2(image, width);
	}

	// 同时写入上一帧缓冲区 (0x26)，确保局部刷新时对比基准正确
	EPD_4in26_SendCommand(0x26);   //write RAM for previous frame
	for(i=0; i<height; i++)
	{
	    EPD_4in26_SendData2(image, width);
	}

	EPD_4in26_TurnOnDisplay_Fast();
}

/******************************************************************************
function :	Quick refresh - sends the image buffer and displays
parameter:	Image - image data buffer
description:
	快速全屏刷新，使用 0xC7 刷新序列，比标准全刷更快。
	同时写入 0x24 和 0x26，为后续局部刷新建立正确的对比基准。
******************************************************************************/
void EPD_4in26_Display_Fast(UBYTE *Image)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;

	ESP_LOGI("EPD", "EPD_4in26_Display_Fast: starting...");

	// 重新设置全屏窗口（防止之前的局部刷新改变了窗口范围）
	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);
	EPD_4in26_SetCursor(0, 0);

	// 温度补偿
	EPD_4in26_SendCommand(0x1A);
	EPD_4in26_SendData(0x5A);
	EPD_4in26_SendCommand(0x24);
	for(i=0; i<height; i++)
	{
        EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}

	EPD_4in26_TurnOnDisplay_Fast();
	ESP_LOGI("EPD", "EPD_4in26_Display_Fast: complete!");
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_4in26_Display(UBYTE *Image)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;

	ESP_LOGI("EPD", "EPD_4in26_Display: starting, height=%u, width=%u bytes", height, width);

	// 重新设置全屏窗口（防止之前的局部刷新改变了窗口范围）
	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);
	EPD_4in26_SetCursor(0, 0);

	// 打印前几行数据用于调试
	ESP_LOGI("EPD", "EPD_4in26_Display: first 4 bytes of image: 0x%02X 0x%02X 0x%02X 0x%02X",
	         Image[0], Image[1], Image[2], Image[3]);

	// 写入当前图像缓冲区 (0x24)
	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
        EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}

	ESP_LOGI("EPD", "EPD_4in26_Display: 0x24 written, writing 0x26...");

	// 同时写入上一帧缓冲区 (0x26)，确保局部刷新时对比正确
	// 这是关键：如果不写 0x26，局部刷新会和旧数据对比，导致显示错误
	EPD_4in26_SendCommand(0x26);   //write RAM for previous frame
	for(i=0; i<height; i++)
	{
        EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}

	ESP_LOGI("EPD", "EPD_4in26_Display: both RAMs written, triggering display...");
	EPD_4in26_TurnOnDisplay();
	ESP_LOGI("EPD", "EPD_4in26_Display: complete!");
}

// 优化：流式发送局部刷新（重构版）
// 直接从完整的 framebuffer 按行发送，避免使用中间缓冲区
// 坐标对齐和参数验证已移入函数内部，简化调用代码
// 参数：
//   full_framebuffer: 完整的 800x480 framebuffer
//   fb_stride: framebuffer 的行字节宽度 (800/8 = 100)
//   x, y: 局部刷新区域的起始坐标（EPD 物理坐标）
//   w, h: 局部刷新区域的宽度和高度（像素）
static void EPD_4in26_Display_Part_Stream_Impl(UBYTE *full_framebuffer, uint32_t fb_stride,
											   UWORD x, UWORD y, UWORD w, UWORD h)
{
	if (w == 0 || h == 0) {
		ESP_LOGW("EPD_PART", "[WARN] Invalid size: w=%u, h=%u", w, h);
		return;
	}

	// ============================================
	// 1. 参数验证和边界检查
	// ============================================
	if (x >= EPD_4in26_WIDTH || y >= EPD_4in26_HEIGHT) {
		ESP_LOGE("EPD_PART", "[ERROR] Invalid start position: x=%u, y=%u (max=%dx%d)",
		         x, y, EPD_4in26_WIDTH-1, EPD_4in26_HEIGHT-1);
		return;
	}

	// ============================================
	// 2. X 坐标按 8 像素对齐（EPD 硬件要求）
	// ============================================
	// Arduino 代码：x_start=x_start-x_start%8;
	UWORD x_aligned = x - (x % 8);  // 向下对齐到 8 像素边界

	// 关键差异：Arduino 基于已对齐的 x_start 计算 x_end
	// Arduino: x_end=x_start+PART_LINE-1;
	UWORD x_end = x_aligned + w - 1;  // 使用 x_aligned 而不是原始 x

	if (x_end >= EPD_4in26_WIDTH) {
		x_end = EPD_4in26_WIDTH - 1;
	}

	// 计算实际宽度（对齐后）
	UWORD w_aligned = x_end - x_aligned + 1;

	// ============================================
	// 3. Y 坐标边界检查
	// ============================================
	UWORD y_end = y + h - 1;
	if (y_end >= EPD_4in26_HEIGHT) {
		y_end = EPD_4in26_HEIGHT - 1;
	}
	UWORD h_actual = y_end - y + 1;  // 实际高度

	// 计算字节宽度和偏移
	UWORD x_offset_bytes = x_aligned / 8;           // X 起始字节偏移
	UWORD w_bytes = (w_aligned + 7) / 8;            // 对齐后的字节宽度

	// ============================================
	// 4. 调试信息
	// ============================================
	ESP_LOGI("EPD_PART", "[INFO] Partial refresh: x=%u->%u, y=%u->%u, w=%u->%u, h=%u",
	         x, x_aligned, y, y, w, w_aligned, h_actual);
	ESP_LOGI("EPD_PART", "[INFO] x_offset=%u bytes, w_bytes=%u", x_offset_bytes, w_bytes);

#ifdef EPD_PART_DEBUG
	// 打印前 3 行的数据（仅在调试时启用）
	for (int debug_row = 0; debug_row < 3 && debug_row < h_actual; debug_row++) {
		UBYTE *row_ptr = full_framebuffer + (y + debug_row) * fb_stride + x_offset_bytes;
		char hex_str[64] = {0};
		int len = w_bytes < 8 ? w_bytes : 8;
		for (int j = 0; j < len && j*2 < 62; j++) {
			snprintf(hex_str + j*2, 3, "%02X", row_ptr[j]);
		}
		ESP_LOGI("EPD_PART", "[DEBUG] Row %d: %s%s", debug_row, hex_str, w_bytes > 8 ? "..." : "");
	}
#endif

	// ============================================
	// 5. 发送 EPD 局部刷新命令序列（参考 Arduino 示例）
	// ============================================

	EPD_4in26_SendCommand(0x18); // use the internal temperature sensor
	EPD_4in26_SendData(0x80);

	EPD_4in26_SendCommand(0x3C); // BorderWavefrom
	EPD_4in26_SendData(0x80);

	// ============================================
	// **重要**: 这块屏幕的Y坐标是反向的！
	// 参考 GxEPD2_426_GDEQ0426T82::_setPartialRamArea
	// 1. 设置 data entry mode 为 X增Y减
	// 2. 翻转 Y 坐标: y_reversed = HEIGHT - y - h
	// ============================================
	EPD_4in26_SendCommand(0x11); // set ram entry mode
	EPD_4in26_SendData(0x01);    // x increase, y decrease : y reversed
	
	UWORD y_reversed = EPD_4in26_HEIGHT - y - h_actual;
	UWORD y_end_reversed = y_reversed + h_actual - 1;

	ESP_LOGI("EPD_COORD", "Y-reversal: log_y=%u -> phy_y=[%u,%u]", y, y_reversed, y_end_reversed);

	// 关键点：当前采用 Y 递减模式 (0x11=0x01)
	// 因此窗口/光标必须用“从大到小”的 Y 顺序：Ystart = y_end_reversed, Yend = y_reversed
	EPD_4in26_SetWindows(x_aligned, y_end_reversed, x_end, y_reversed);
	EPD_4in26_SetCursor(x_aligned, y_end_reversed);

	EPD_4in26_SendCommand(0x24);   // Write Black and White image to RAM

	// ============================================
	// 6. 流式发送：直接从 framebuffer 逐行发送
	// ============================================
	// 调试：打印前3行的数据
	if (h_actual >= 3) {
		for (int debug_row = 0; debug_row < 3; debug_row++) {
			UBYTE *row_ptr = full_framebuffer + (y + debug_row) * fb_stride + x_offset_bytes;
			char hex_str[32] = {0};
			int len = w_bytes < 8 ? w_bytes : 8;
			for (int j = 0; j < len && j*3 < 30; j++) {
				snprintf(hex_str + j*3, 4, "%02X ", row_ptr[j]);
			}
			ESP_LOGI("EPD_PART", "[DATA] Row %d: %s%s", debug_row, hex_str, w_bytes > 8 ? "..." : "");
		}
	}

	EPD_4in26_SendRegion_FromFramebuffer(full_framebuffer, fb_stride, x_offset_bytes, w_bytes, y, h_actual);

	// ============================================
	// 7. 触发局部刷新显示（参考 Arduino EPD_Part_Update）
	// ============================================
	EPD_4in26_TurnOnDisplay_Part();
	//EPD_4in26_TurnOnDisplay_Fast();
}

void EPD_4in26_Display_Part_Stream(UBYTE *full_framebuffer, uint32_t fb_stride,
                                   UWORD x, UWORD y, UWORD w, UWORD h)
{
	EPD_4in26_Display_Part_Stream_Impl(full_framebuffer, fb_stride, x, y, w, h);
}

/******************************************************************************
function :	Enter deep sleep mode (Step 6 in flowchart)
parameter:
******************************************************************************/
void EPD_4in26_Sleep(void)
{
	// Step 6: Power Off
	// Deep sleep by Command 0x10
	EPD_4in26_SendCommand(0x10); // Enter deep sleep
	EPD_4in26_SendData(0x03);    // Deep sleep mode
	DEV_Delay_ms(100);
	// Note: Power OFF handled by hardware if needed
}

/******************************************************************************
function :	Wake up from deep sleep mode
parameter:
******************************************************************************/
void EPD_4in26_Wakeup(void)
{
	// Exit deep sleep by hardware reset
	EPD_4in26_Reset();
	DEV_Delay_ms(10);

	// Send software reset to ensure clean state
	EPD_4in26_SendCommand(0x12);  // SWRESET
	EPD_4in26_ReadBusy();
	DEV_Delay_ms(10);
}

/******************************************************************************
function :	Public API - Get EPD temperature
parameter:	Return current EPD temperature in degrees Celsius
******************************************************************************/
int EPD_4in26_GetTemperature(void)
{
	return EPD_4in26_ReadTemperature();
}

/******************************************************************************
function :	Public API - Load temperature compensated waveform LUT
parameter:	Automatically selects LUT based on current temperature
******************************************************************************/
void EPD_4in26_Load_Temperature_LUT(void)
{
	EPD_4in26_WriteLUT_TemperatureCompensated();
}

/******************************************************************************
function :	Public API - Load fast refresh waveform LUT
parameter:	Loads WS_80_127 LUT for fast partial refresh
******************************************************************************/
void EPD_4in26_Load_Fast_LUT(void)
{
	EPD_4in26_WriteLUT_Fast();
}
