#include "Display_EPD_W21_spi.h"
#include <SPI.h>

//SPI write byte
/*void SPI_Write(unsigned char value)
{				   			 
   SPI.transfer(value);
}
*/
void GPIO_IO(unsigned char i) 
{
	if(i==0)
	{		
     pinMode(23, INPUT); //SDA 
	}
	else
	{
     pinMode(23, OUTPUT); //SDA 
	}
}
void SPI_Delay(unsigned char xrate)
{
  unsigned char i;
  while(xrate)
  {
    for(i=0;i<2;i++);
    xrate--;
  }
}
void SPI_Write(unsigned char value)                                    
{                                                           
    unsigned char i;  
   SPI_Delay(1);
    for(i=0; i<8; i++)   
    {
        EPD_W21_CLK_0;
       SPI_Delay(1);
       if(value & 0x80)
          EPD_W21_MOSI_1;
        else
          EPD_W21_MOSI_0;   
        value = (value << 1); 
       SPI_Delay(1);
        EPD_W21_CLK_1; 
        SPI_Delay(1);
    }
}
//SPI write command
void EPD_W21_WriteCMD(unsigned char command)
{
	EPD_W21_CS_0;
	EPD_W21_DC_0;  // D/C#   0:command  1:data  
	SPI_Write(command);
	EPD_W21_CS_1;
}
//SPI write data
void EPD_W21_WriteDATA(unsigned char datas)
{
	EPD_W21_CS_0;
	EPD_W21_DC_1;  // D/C#   0:command  1:data
	SPI_Write(datas);
	EPD_W21_CS_1;
}
unsigned char EPD_W21_ReadDATA(void)
{
	
	unsigned char i,j=0;
	 GPIO_IO(0); 
    EPD_W21_CS_0;                   
	  EPD_W21_DC_1;		// command write(Must be added)
	  EPD_W21_MOSI_1;
    SPI_Delay(2);
     for(i=0; i<8; i++)   
    {
    EPD_W21_CLK_0;
		SPI_Delay(20);
		j=(j<<1);
        if(EPD_W21_READ==1)    	
			j|=0x01;
        else
    j&=~0x01;		 
		SPI_Delay(20);
    EPD_W21_CLK_1; 
    SPI_Delay(5);
    }
	EPD_W21_CS_1;
	GPIO_IO(1); 
	return(j);
}

