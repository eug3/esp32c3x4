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

const unsigned char LUT_DATA_4Gray[112] =    //112bytes
{											
0x80,	0x48,	0x4A,	0x22,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x0A,	0x48,	0x68,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x88,	0x48,	0x60,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA8,	0x48,	0x45,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x07,	0x1E,	0x1C,	0x02,	0x00,						
0x05,	0x01,	0x05,	0x01,	0x02,						
0x08,	0x01,	0x01,	0x04,	0x04,						
0x00,	0x02,	0x00,	0x02,	0x01,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x30,						
0x00,	0x00,	
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

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
void EPD_4in26_ReadBusy(void)
{
    Debug("e-Paper busy\r\n");
	while(1)
	{	 //=1 BUSY
		if(DEV_Digital_Read(EPD_BUSY_PIN)==0) 
			break;
		DEV_Delay_ms(20);
	}
	DEV_Delay_ms(20);
    Debug("e-Paper busy release\r\n");
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
	EPD_4in26_SendData(0xC7);
	EPD_4in26_SendCommand(0x20); //Activate Display Update Sequence
	EPD_4in26_ReadBusy();
}

static void EPD_4in26_TurnOnDisplay_Part(void)
{
	EPD_4in26_SendCommand(0x22); //Display Update Control
	EPD_4in26_SendData(0xFF);
	EPD_4in26_SendCommand(0x20); //Activate Display Update Sequence
	EPD_4in26_ReadBusy();
}

static void EPD_4in26_TurnOnDisplay_4GRAY(void)
{
    EPD_4in26_SendCommand(0x22);
    EPD_4in26_SendData(0xC7);
	EPD_4in26_SendCommand(0x20);
    EPD_4in26_ReadBusy();
}

static void EPD_4in26_TurnOnDisplay_4GRAY_Part(void)
{
	// Reuse the partial update sequence to avoid full-screen flashing.
	// Note: 4-gray partial update behavior can vary across panels/waveforms.
	EPD_4in26_SendCommand(0x22);
	EPD_4in26_SendData(0xFF);
	EPD_4in26_SendCommand(0x20);
	EPD_4in26_ReadBusy();
}

static inline UBYTE EPD_4in26_4gray_pixel_to_plane24(const UBYTE p2)
{
	// Mapping copied from EPD_4in26_4GrayDisplay()'s 0x24 pass
	// p2 is in {0x00,0x40,0x80,0xC0}
	if (p2 == 0xC0) return 0;
	if (p2 == 0x00) return 1;
	if (p2 == 0x80) return 1;
	return 0; // 0x40
}

static inline UBYTE EPD_4in26_4gray_pixel_to_plane26(const UBYTE p2)
{
	// Mapping copied from EPD_4in26_4GrayDisplay()'s 0x26 pass
	if (p2 == 0xC0) return 0; // white
	if (p2 == 0x00) return 1; // black
	if (p2 == 0x80) return 0; // gray1
	return 1;                 // gray2 (0x40)
}

static inline UBYTE EPD_4in26_pack8_2bpp_to_1bpp_plane(UBYTE in0, UBYTE in1, bool plane26)
{
	// Each input byte encodes 4 pixels (2bpp). Two input bytes => 8 pixels.
	// Produce one 1bpp output byte (MSB first), matching the original algorithm.
	UBYTE out = 0;
	for (int i = 0; i < 8; i++) {
		const UBYTE src = (i < 4) ? in0 : in1;
		const int shift = 6 - 2 * (i & 3);
		const UBYTE p = (UBYTE)((src >> shift) & 0x03);
		const UBYTE p2_norm = (UBYTE)(p << 6); // 0x00/0x40/0x80/0xC0
		const UBYTE bit = plane26 ? EPD_4in26_4gray_pixel_to_plane26(p2_norm)
								  : EPD_4in26_4gray_pixel_to_plane24(p2_norm);
		out = (UBYTE)((out << 1) | (bit & 0x01));
	}
	return out;
}

/******************************************************************************
function :	set the look-up tables
parameter:
******************************************************************************/
static void EPD_4in26_Lut(void)
{
    unsigned int count;
    EPD_4in26_SendCommand(0x32); //vcom
    for(count = 0; count < 105 ; count++) {
        EPD_4in26_SendData(LUT_DATA_4Gray[count]);
    }

    EPD_4in26_SendCommand(0x03); //VGH      
	EPD_4in26_SendData(LUT_DATA_4Gray[105]);

	EPD_4in26_SendCommand(0x04); //      
	EPD_4in26_SendData(LUT_DATA_4Gray[106]); //VSH1   
	EPD_4in26_SendData(LUT_DATA_4Gray[107]); //VSH2   
	EPD_4in26_SendData(LUT_DATA_4Gray[108]); //VSL   

	EPD_4in26_SendCommand(0x2C);     //VCOM Voltage
	EPD_4in26_SendData(LUT_DATA_4Gray[109]);    //0x1C
}

/******************************************************************************
function :	Setting the display window
parameter:
******************************************************************************/
static void EPD_4in26_SetWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
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
    EPD_4in26_SendCommand(0x4E); // SET_RAM_X_ADDRESS_COUNTER
    EPD_4in26_SendData(Xstart & 0xFF);
    EPD_4in26_SendData((Xstart>>8) & 0x03);

    EPD_4in26_SendCommand(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
    EPD_4in26_SendData(Ystart & 0xFF);
    EPD_4in26_SendData((Ystart>>8) & 0x03);
}

/******************************************************************************
function :	Initialize the e-Paper register
parameter:
******************************************************************************/
void EPD_4in26_Init(void)
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

	EPD_4in26_ReadBusy();
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

	EPD_4in26_ReadBusy();

	//TEMP (1.5s)
	EPD_4in26_SendCommand(0x1A);  
    EPD_4in26_SendData(0x5A); 

    EPD_4in26_SendCommand(0x22);  
    EPD_4in26_SendData(0x91); 
    EPD_4in26_SendCommand(0x20); 
	
	EPD_4in26_ReadBusy();
}

void EPD_4in26_Init_4GRAY(void)
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
	EPD_4in26_SendData((EPD_4in26_WIDTH-1)%256); //  Y  
	EPD_4in26_SendData((EPD_4in26_WIDTH-1)/256); //  Y 
	EPD_4in26_SendData(0x02);

	EPD_4in26_SendCommand(0x3C);        // Border       Border setting 
	EPD_4in26_SendData(0x01);

	EPD_4in26_SendCommand(0x11);        //    data  entry  mode
	EPD_4in26_SendData(0x01);           //       X-mode  x+ y- (original)

	EPD_4in26_SetWindows(0, EPD_4in26_HEIGHT-1, EPD_4in26_WIDTH-1, 0);

	EPD_4in26_SetCursor(0, 0);

	EPD_4in26_ReadBusy();

    EPD_4in26_Lut();
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
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_4in26_Display(UBYTE *Image)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;
	
	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
        EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}
	EPD_4in26_TurnOnDisplay();	
}

void EPD_4in26_Display_Base(UBYTE *Image)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;
	
	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
		EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}

	EPD_4in26_SendCommand(0x26);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
		EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}
	EPD_4in26_TurnOnDisplay();	
}

void EPD_4in26_Display_Fast(UBYTE *Image)
{
	UWORD i;
	UWORD height = EPD_4in26_HEIGHT;
	UWORD width = EPD_4in26_WIDTH/8;
	
	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
		EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}
	EPD_4in26_TurnOnDisplay_Fast();	
}

void EPD_4in26_Display_Part(UBYTE *Image, UWORD x, UWORD y, UWORD w, UWORD l)
{
	UWORD i;
	UWORD height = l;
	UWORD width =  (w % 8 == 0)? (w / 8 ): (w / 8 + 1);

    EPD_4in26_Reset();

	EPD_4in26_SendCommand(0x18); // use the internal temperature sensor
	EPD_4in26_SendData(0x80);

	EPD_4in26_SendCommand(0x3C);        // Border       Border setting 
	EPD_4in26_SendData(0x80);

	EPD_4in26_SetWindows(x, y, x+w-1, y+l-1);

	EPD_4in26_SetCursor(x, y);

	EPD_4in26_SendCommand(0x24);   //write RAM for black(0)/white (1)
	for(i=0; i<height; i++)
	{
		EPD_4in26_SendData2((UBYTE *)(Image+i*width), width);
	}
	EPD_4in26_TurnOnDisplay_Part();	
}

void EPD_4in26_4GrayDisplay(UBYTE *Image)
{
    UDOUBLE i,j,k;
    UBYTE temp1,temp2,temp3;

    // old  data
    EPD_4in26_SendCommand(0x24);
    for(i=0; i<48000; i++) {             //5808*4  46464
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;
                else if(temp2 == 0x00)
                    temp3 |= 0x01; 
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else //0x40
                    temp3 |= 0x00; 
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0) 
                    temp3 |= 0x00;
                else if(temp2 == 0x00) 
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else    //0x40
                    temp3 |= 0x00;	
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }

        }
        EPD_4in26_SendData(temp3);
        // printf("%x",temp3);
    }

    EPD_4in26_SendCommand(0x26);   //write RAM for black(0)/white (1)
    for(i=0; i<48000; i++) {             //5808*4  46464
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;//white
                else if(temp2 == 0x00)
                    temp3 |= 0x01;  //black
                else if(temp2 == 0x80)
                    temp3 |= 0x00;  //gray1
                else //0x40
                    temp3 |= 0x01; //gray2
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)  //white
                    temp3 |= 0x00;
                else if(temp2 == 0x00) //black
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x00; //gray1
                else    //0x40
                    temp3 |= 0x01;	//gray2
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }
        }
        EPD_4in26_SendData(temp3);
        // printf("%x",temp3);
    }

    EPD_4in26_TurnOnDisplay_4GRAY();
}

void EPD_4in26_4GrayDisplay_Part(UBYTE *Image, UWORD x, UWORD y, UWORD w, UWORD l)
{
	// Note: This function assumes Image is a 2bpp (Paint scale=4) buffer with
	// stride = EPD_4in26_WIDTH/4 bytes per row.
	// For safe conversion, x should be 8-pixel aligned and w should be a multiple of 8.

	if (w == 0 || l == 0) return;
	if (x >= EPD_4in26_WIDTH || y >= EPD_4in26_HEIGHT) return;

	if (x + w > EPD_4in26_WIDTH)  w = EPD_4in26_WIDTH - x;
	if (y + l > EPD_4in26_HEIGHT) l = EPD_4in26_HEIGHT - y;

	const UWORD x_aligned = (UWORD)(x & ~0x7);
	UWORD x_end = (UWORD)(x + w - 1);
	x_end = (UWORD)((x_end + 7) & ~0x7);
	if (x_end >= EPD_4in26_WIDTH) x_end = EPD_4in26_WIDTH - 1;

	const UWORD w_aligned = (UWORD)(x_end - x_aligned + 1);
	const UWORD out_w_bytes = (UWORD)((w_aligned + 7) / 8);
	const UWORD out_x_byte = (UWORD)(x_aligned / 8);

	const UWORD in_stride = (UWORD)(EPD_4in26_WIDTH / 4);   // 2bpp => 4 pixels/byte
	const UWORD in_x_byte = (UWORD)(out_x_byte * 2);        // 8 pixels => 2 bytes in 2bpp

	// Match the partial path's border behavior
	EPD_4in26_SendCommand(0x3C);
	EPD_4in26_SendData(0x80);

	EPD_4in26_SetWindows(x_aligned, y, (UWORD)(x_aligned + w_aligned - 1), (UWORD)(y + l - 1));
	EPD_4in26_SetCursor(x_aligned, y);

	// Plane 0x24
	EPD_4in26_SendCommand(0x24);
	for (UWORD row = 0; row < l; row++) {
		const uint32_t base = (uint32_t)(y + row) * (uint32_t)in_stride + (uint32_t)in_x_byte;
		for (UWORD col = 0; col < out_w_bytes; col++) {
			const uint32_t idx = base + (uint32_t)col * 2;
			const UBYTE in0 = Image[idx];
			const UBYTE in1 = Image[idx + 1];
			const UBYTE out = EPD_4in26_pack8_2bpp_to_1bpp_plane(in0, in1, false);
			EPD_4in26_SendData(out);
		}
	}

	// Plane 0x26
	EPD_4in26_SendCommand(0x26);
	for (UWORD row = 0; row < l; row++) {
		const uint32_t base = (uint32_t)(y + row) * (uint32_t)in_stride + (uint32_t)in_x_byte;
		for (UWORD col = 0; col < out_w_bytes; col++) {
			const uint32_t idx = base + (uint32_t)col * 2;
			const UBYTE in0 = Image[idx];
			const UBYTE in1 = Image[idx + 1];
			const UBYTE out = EPD_4in26_pack8_2bpp_to_1bpp_plane(in0, in1, true);
			EPD_4in26_SendData(out);
		}
	}

	EPD_4in26_TurnOnDisplay_4GRAY_Part();
}

/******************************************************************************
function :	Enter sleep mode
parameter:
******************************************************************************/
void EPD_4in26_Sleep(void)
{
	EPD_4in26_SendCommand(0x10); //enter deep sleep
	EPD_4in26_SendData(0x03); 
	DEV_Delay_ms(100);
}
