/*****************************************************************************
* | File      	:   EPD_4in26.h
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
#ifndef __EPD_4in26_H_
#define __EPD_4in26_H_

#include "DEV_Config.h"

// Display resolution
#define EPD_4in26_WIDTH       800
#define EPD_4in26_HEIGHT      480

void EPD_4in26_Init(void);
void EPD_4in26_Init_Fast(void);
void EPD_4in26_Init_4GRAY(void);
void EPD_4in26_Clear(void);
void EPD_4in26_Clear_Fast(void);
void EPD_4in26_Display(UBYTE *Image);
void EPD_4in26_Display_Base(UBYTE *Image);
void EPD_4in26_Display_Fast(UBYTE *Image);
void EPD_4in26_4GrayDisplay(UBYTE *Image);
void EPD_4in26_Sleep(void);
void EPD_4in26_Wakeup(void);

// GxEPD2 风格的局部刷新（根据 GxEPD2_426_GDEQ0426T82）
// 使用 0x22+0xFC 进行快刷局部更新
void EPD_4in26_Display_Partial(UBYTE *Image, UWORD x, UWORD y, UWORD w, UWORD h);

// 局部刷新（非流式版本，重构版）
// 使用独立的数据缓冲区（仅包含要刷新的区域数据）
// 坐标对齐和参数验证已移入函数内部，简化调用
void EPD_4in26_Display_Part(UBYTE *Image, UWORD x, UWORD y, UWORD w, UWORD h);

// 局部刷新（流式版本）
// 直接从完整framebuffer读取指定区域并发送
// full_framebuffer: 完整的800x480帧缓冲
// fb_stride: 每行字节数（800/8=100）
// x, y, w, h: 物理坐标系下的刷新区域
void EPD_4in26_Display_Part_Stream(UBYTE *full_framebuffer, uint32_t fb_stride,
                                   UWORD x, UWORD y, UWORD w, UWORD h);

// 快刷局刷（流式版本）
// 与 EPD_4in26_Display_Part_Stream 相同的数据写入方式，但使用 fast update 序列触发刷新
void EPD_4in26_Display_Part_Stream_Fast(UBYTE *full_framebuffer, uint32_t fb_stride,
                                        UWORD x, UWORD y, UWORD w, UWORD h);

// 波形 LUT 控制函数（新增）
// 手动读取并返回当前 EPD 温度传感器值（摄氏度）
int EPD_4in26_GetTemperature(void);

// 手动加载温度补偿的波形 LUT（根据当前温度自动选择）
void EPD_4in26_Load_Temperature_LUT(void);

// 手动加载快刷波形 LUT
void EPD_4in26_Load_Fast_LUT(void);

#endif
