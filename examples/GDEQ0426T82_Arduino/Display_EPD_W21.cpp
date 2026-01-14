#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"

////////////////////////////////////E-paper demo//////////////////////////////////////////////////////////
//Busy function
void Epaper_READBUSY(void)
{ 
  while(1)
  {  //=1 BUSY
     if(isEPD_W21_BUSY==0) break;
  }  
}
//Full screen refresh initialization
void EPD_HW_Init(void)
{
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 
  
  Epaper_READBUSY();   
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();   

  EPD_W21_WriteCMD(0x18);   
  EPD_W21_WriteDATA(0x80); 
  
  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x01); //Driver output control      
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);    
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);
  EPD_W21_WriteDATA(0x02); 

  EPD_W21_WriteCMD(0x3C); //BorderWavefrom
  EPD_W21_WriteDATA(0x01);  
  
  EPD_W21_WriteCMD(0x11); //data entry mode       
  EPD_W21_WriteDATA(0x03);

  EPD_W21_WriteCMD(0x44); //set Ram-X address start/end position   
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_HEIGHT-1)%256);   
  EPD_W21_WriteDATA((EPD_HEIGHT-1)/256);

  EPD_W21_WriteCMD(0x45); //set Ram-Y address start/end position          
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00); 
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);   
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);


  EPD_W21_WriteCMD(0x4E);   // set RAM x address count to 0;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteCMD(0x4F);   // set RAM y address count to 0X199;    
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  Epaper_READBUSY();
  
}
//Fast refresh 1 initialization
void EPD_HW_Init_Fast(void)
{
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 
  
  Epaper_READBUSY();   
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();   

  EPD_W21_WriteCMD(0x18);   
  EPD_W21_WriteDATA(0x80); 
  
  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x01); //Driver output control      
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);   
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);
  EPD_W21_WriteDATA(0x02);

  EPD_W21_WriteCMD(0x3C); //BorderWavefrom
  EPD_W21_WriteDATA(0x01);  
  
  EPD_W21_WriteCMD(0x11); //data entry mode       
  EPD_W21_WriteDATA(0x03);

  EPD_W21_WriteCMD(0x44); //set Ram-X address start/end position   
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_HEIGHT-1)%256);    
  EPD_W21_WriteDATA((EPD_HEIGHT-1)/256);

  EPD_W21_WriteCMD(0x45); //set Ram-Y address start/end position          
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00); 
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);    
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);


  EPD_W21_WriteCMD(0x4E);   // set RAM x address count to 0;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteCMD(0x4F);   // set RAM y address count to 0X199;    
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  Epaper_READBUSY();

  //TEMP (1.5s)
  EPD_W21_WriteCMD(0x1A); 
  EPD_W21_WriteDATA(0x5A);

  EPD_W21_WriteCMD(0x22); 
  EPD_W21_WriteDATA(0x91);
  EPD_W21_WriteCMD(0x20);

  Epaper_READBUSY();  
}


//////////////////////////////Display Update Function///////////////////////////////////////////////////////
/*
//Full screen refresh update function
void EPD_Update(void)
{   
  EPD_W21_WriteCMD(0x22); //Display Update Control
  EPD_W21_WriteDATA(0xF7);   
  EPD_W21_WriteCMD(0x20); //Activate Display Update Sequence
  Epaper_READBUSY();   

}
//Fast refresh 1 update function
void EPD_Update_Fast(void)
{   
  EPD_W21_WriteCMD(0x22); //Display Update Control
  EPD_W21_WriteDATA(0xC7);   
  EPD_W21_WriteCMD(0x20); //Activate Display Update Sequence
  Epaper_READBUSY();   

}
*/
//Partial refresh update function
void EPD_Part_Update(void)
{
  EPD_W21_WriteCMD(0x22); //Display Update Control
  EPD_W21_WriteDATA(0xFF);   
  EPD_W21_WriteCMD(0x20); //Activate Display Update Sequence
  Epaper_READBUSY();      
}
//////////////////////////////Display Data Transfer Function////////////////////////////////////////////
//Full screen refresh display function
void EPD_WhiteScreen_ALL(const unsigned char *datas)
{
   unsigned int i;  
  EPD_W21_WriteCMD(0x24);   //write RAM for black(0)/white (1)
  for(i=0;i<EPD_ARRAY;i++)
   {               
     EPD_W21_WriteDATA(datas[i]);
   }   
   EPD_Update();   
}
//Fast refresh display function
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas)
{
   unsigned int i;  
  EPD_W21_WriteCMD(0x24);   //write RAM for black(0)/white (1)
   for(i=0;i<EPD_ARRAY;i++)
   {               
     EPD_W21_WriteDATA(datas[i]);
   }     
   EPD_Update_Fast();  
}

//Clear screen display
void EPD_WhiteScreen_White(void)
{
 unsigned int i;
 EPD_W21_WriteCMD(0x24);   //write RAM for black(0)/white (1)
 for(i=0;i<EPD_ARRAY;i++)
 {
    EPD_W21_WriteDATA(0xff);
  }
  EPD_Update();
}
//Display all black
void EPD_WhiteScreen_Black(void)
{
 unsigned int i;
 EPD_W21_WriteCMD(0x24);   //write RAM for black(0)/white (1)
 for(i=0;i<EPD_ARRAY;i++)
 {
    EPD_W21_WriteDATA(0x00);
  }
  EPD_Update();
}
//Partial refresh of background display, this function is necessary, please do not delete it!!!
void EPD_SetRAMValue_BaseMap( const unsigned char * datas)
{
  unsigned int i;     
  EPD_W21_WriteCMD(0x24);   //Write Black and White image to RAM
  for(i=0;i<EPD_ARRAY;i++)
   {               
     EPD_W21_WriteDATA(datas[i]);
   }
  EPD_W21_WriteCMD(0x26);   //Write Black and White image to RAM
  for(i=0;i<EPD_ARRAY;i++)
   {               
     EPD_W21_WriteDATA(datas[i]);
   }
   EPD_Update();     
   
}
//Partial refresh display
void EPD_Dis_Part(unsigned int x_start,unsigned int y_start,const unsigned char * datas,unsigned int PART_COLUMN,unsigned int PART_LINE)
{
  unsigned int i;  
  unsigned int x_end,y_end;
  
  x_start=x_start-x_start%8; //x address start
  x_end=x_start+PART_LINE-1; //x address end
  y_start=y_start; //Y address start
  y_end=y_start+PART_COLUMN-1; //Y address end
  
//Reset
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 

  EPD_W21_WriteCMD(0x18); 
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x3C); //BorderWavefrom  
  EPD_W21_WriteDATA(0x80);  
//  
  
  EPD_W21_WriteCMD(0x44);       // set RAM x address start/end
  EPD_W21_WriteDATA(x_start%256);  //x address start2 
  EPD_W21_WriteDATA(x_start/256); //x address start1 
  EPD_W21_WriteDATA(x_end%256);  //x address end2 
  EPD_W21_WriteDATA(x_end/256); //x address end1   
  EPD_W21_WriteCMD(0x45);    // set RAM y address start/end
  EPD_W21_WriteDATA(y_start%256);  //y address start2 
  EPD_W21_WriteDATA(y_start/256); //y address start1 
  EPD_W21_WriteDATA(y_end%256);  //y address end2 
  EPD_W21_WriteDATA(y_end/256); //y address end1   

  EPD_W21_WriteCMD(0x4E);        // set RAM x address count to 0;
  EPD_W21_WriteDATA(x_start%256);  //x address start2 
  EPD_W21_WriteDATA(x_start/256); //x address start1 
  EPD_W21_WriteCMD(0x4F);      // set RAM y address count to 0X127;    
  EPD_W21_WriteDATA(y_start%256);//y address start2
  EPD_W21_WriteDATA(y_start/256);//y address start1
  
  
   EPD_W21_WriteCMD(0x24);   //Write Black and White image to RAM
   for(i=0;i<PART_COLUMN*PART_LINE/8;i++)
   {                         
     EPD_W21_WriteDATA(datas[i]);
   } 
   EPD_Part_Update();

}
//Full screen partial refresh display
void EPD_Dis_PartAll(const unsigned char * datas)
{
  unsigned int i;  
  unsigned int PART_COLUMN, PART_LINE;
  PART_COLUMN=EPD_HEIGHT,PART_LINE=EPD_WIDTH;

//Reset
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 

  EPD_W21_WriteCMD(0x18); 
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x3C); //BorderWavefrom  
  EPD_W21_WriteDATA(0x80);  
//  
  
  EPD_W21_WriteCMD(0x24);   //Write Black and White image to RAM
   for(i=0;i<PART_COLUMN*PART_LINE/8;i++)
   {                         
     EPD_W21_WriteDATA(datas[i]);
   } 
   EPD_Part_Update();

}
//Deep sleep function
void EPD_DeepSleep(void)
{   
  EPD_W21_WriteCMD(0x10); //Enter deep sleep
  EPD_W21_WriteDATA(0x01); 
  delay(100);
}

//Partial refresh write address and data
void EPD_Dis_Part_RAM(unsigned int x_start,unsigned int y_start,const unsigned char * datas,unsigned int PART_COLUMN,unsigned int PART_LINE)
{
  unsigned int i;  
  unsigned int x_end,y_end;
  
  x_start=x_start-x_start%8; //x address start
  x_end=x_start+PART_LINE-1; //x address end
  y_start=y_start; //Y address start
  y_end=y_start+PART_COLUMN-1; //Y address end
  
//Reset
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 

  EPD_W21_WriteCMD(0x18); 
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x3C); //BorderWavefrom  
  EPD_W21_WriteDATA(0x80);  
//  
  
  EPD_W21_WriteCMD(0x44);       // set RAM x address start/end
  EPD_W21_WriteDATA(x_start%256);  //x address start2 
  EPD_W21_WriteDATA(x_start/256); //x address start1 
  EPD_W21_WriteDATA(x_end%256);  //x address end2 
  EPD_W21_WriteDATA(x_end/256); //x address end1   
  EPD_W21_WriteCMD(0x45);    // set RAM y address start/end
  EPD_W21_WriteDATA(y_start%256);  //y address start2 
  EPD_W21_WriteDATA(y_start/256); //y address start1 
  EPD_W21_WriteDATA(y_end%256);  //y address end2 
  EPD_W21_WriteDATA(y_end/256); //y address end1   

  EPD_W21_WriteCMD(0x4E);        // set RAM x address count to 0;
  EPD_W21_WriteDATA(x_start%256);  //x address start2 
  EPD_W21_WriteDATA(x_start/256); //x address start1 
  EPD_W21_WriteCMD(0x4F);      // set RAM y address count to 0X127;    
  EPD_W21_WriteDATA(y_start%256);//y address start2
  EPD_W21_WriteDATA(y_start/256);//y address start1
    
  EPD_W21_WriteCMD(0x24);   //Write Black and White image to RAM
  for(i=0;i<PART_COLUMN*PART_LINE/8;i++)
   {                         
     EPD_W21_WriteDATA(datas[i]);
   } 
}
//Clock display
void EPD_Dis_Part_Time(unsigned int x_startA,unsigned int y_startA,const unsigned char * datasA,
                         unsigned int x_startB,unsigned int y_startB,const unsigned char * datasB,
                         unsigned int x_startC,unsigned int y_startC,const unsigned char * datasC,
                         unsigned int x_startD,unsigned int y_startD,const unsigned char * datasD,
                         unsigned int x_startE,unsigned int y_startE,const unsigned char * datasE,
                         unsigned int PART_COLUMN,unsigned int PART_LINE
                        )
{
  EPD_Dis_Part_RAM(x_startA,y_startA,datasA,PART_COLUMN,PART_LINE);
  EPD_Dis_Part_RAM(x_startB,y_startB,datasB,PART_COLUMN,PART_LINE);
  EPD_Dis_Part_RAM(x_startC,y_startC,datasC,PART_COLUMN,PART_LINE);
  EPD_Dis_Part_RAM(x_startD,y_startD,datasD,PART_COLUMN,PART_LINE);
  EPD_Dis_Part_RAM(x_startE,y_startE,datasE,PART_COLUMN,PART_LINE);
  EPD_Part_Update();
}                        




////////////////////////////////Other newly added functions////////////////////////////////////////////
//Display rotation 180 degrees initialization
void EPD_HW_Init_180(void)
{
  EPD_W21_RST_0;  // Module reset   
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10); //At least 10ms delay 
  
  Epaper_READBUSY();   
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();   

  EPD_W21_WriteCMD(0x18);   
  EPD_W21_WriteDATA(0x80); 
  
  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);
  
  EPD_W21_WriteCMD(0x01); //Driver output control      
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);    
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);
  EPD_W21_WriteDATA(0x02);

  EPD_W21_WriteCMD(0x3C); //BorderWavefrom
  EPD_W21_WriteDATA(0x01);  
  
  EPD_W21_WriteCMD(0x11); //data entry mode       
  EPD_W21_WriteDATA(0x00); //180

  EPD_W21_WriteCMD(0x44); //set Ram-X address start/end position   

  EPD_W21_WriteDATA((EPD_HEIGHT-1)%256);    
  EPD_W21_WriteDATA((EPD_HEIGHT-1)/256);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  
  EPD_W21_WriteCMD(0x45); //set Ram-Y address start/end position          
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);    
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00); 



  EPD_W21_WriteCMD(0x4E);   // set RAM x address count to 0;
  EPD_W21_WriteDATA((EPD_HEIGHT-1)%256);    
  EPD_W21_WriteDATA((EPD_HEIGHT-1)/256);
  EPD_W21_WriteCMD(0x4F);   // set RAM y address count to 0X199;    
  EPD_W21_WriteDATA((EPD_WIDTH-1)%256);    
  EPD_W21_WriteDATA((EPD_WIDTH-1)/256);
  Epaper_READBUSY();
}


/*******************************LUT******************************************/
//0--5
 const unsigned char WS_0_5[112] =
{									
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1E,	0x23,	0x21,	0x23,	0x00,						
0x28,	0x01,	0x28,	0x01,	0x03,						
0x1B,	0x19,	0x05,	0x03,	0x01,						
0x05,	0x00,	0x08,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
										
//5--10
 const unsigned char WS_5_10[112] =
{									
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1E,	0x23,	0x05,	0x02,	0x00,						
0x2B,	0x01,	0x2B,	0x01,	0x02,						
0x1B,	0x19,	0x05,	0x03,	0x00,						
0x05,	0x00,	0x07,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										

//10--15
const unsigned char WS_10_15[112] =
{									
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x14,	0x1A,	0x0B,	0x06,	0x00,						
0x21,	0x01,	0x21,	0x01,	0x02,						
0x18,	0x16,	0x05,	0x03,	0x00,						
0x04,	0x00,	0x05,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
										
//15---20
 const unsigned char WS_15_20[112] =
{									
0xA2,	0x48,	0x51,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x48,	0xA8,	0x80,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA2,	0x48,	0x51,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x48,	0xA8,	0x80,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x0D,	0x0D,	0x08,	0x05,	0x00,						
0x0F,	0x01,	0x0F,	0x01,	0x04,						
0x0D,	0x0D,	0x05,	0x05,	0x00,						
0x03,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
		
//20----80
 const unsigned char WS_20_80[112] =
{									
0xA0,	0x48,	0x54,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x50,	0x48,	0xA8,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA0,	0x48,	0x54,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x50,	0x48,	0xA8,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1A,	0x14,	0x00,	0x00,	0x00,						
0x0D,	0x01,	0x0D,	0x01,	0x02,						
0x0A,	0x0A,	0x03,	0x00,	0x01,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
	
								
//80----127  Fast
 const unsigned char WS_80_127[112] =
{									
0xA8,	0x00,	0x55,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x00,	0xAA,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA8,	0x00,	0x55,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x00,	0xAA,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x0C,	0x0D,	0x0B,	0x01,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x0A,	0x0A,	0x05,	0x0B,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x01,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x30,						
0x00,	0x00,									
};											

int Read_temp(void)
{
	int tempvalue;
   unsigned char temp1,temp2;	
   EPD_W21_WriteCMD(0x18);
   EPD_W21_WriteDATA(0X80);
   EPD_W21_WriteCMD(0x22);
   EPD_W21_WriteDATA(0XB1);
   EPD_W21_WriteCMD(0x20);
   Epaper_READBUSY();
   
   EPD_W21_WriteCMD(0x1B); 
    temp1=EPD_W21_ReadDATA();  //temp1£ºD11 D10 D9 D8 D7 D6 D5 D4
    temp2=EPD_W21_ReadDATA();  //temp2£ºD3  D2  D1 D0 0  0  0  0
   

   tempvalue=temp1<<8|temp2;  //Synthesize a 16 bit data
	//D11=0£¬DEC=+ tempvalue/16;  //D11=1,DEX=-tempvalue/16; 
	 tempvalue=tempvalue>>4;   //Move 4 bits to the right to become 12 bits of valid data
	 tempvalue=tempvalue/16; //Output temperature value                           
	return tempvalue;
}

void Write_LUT(const unsigned char *wavefrom)
{
	unsigned char i;
	EPD_W21_WriteCMD(0x32);// write VCOM register
	for(i=0;i<105;i++)
	{
		EPD_W21_WriteDATA(*wavefrom++);
	}
	Epaper_READBUSY();
	
	EPD_W21_WriteCMD(0x03);      
	EPD_W21_WriteDATA(*wavefrom++);
	
	EPD_W21_WriteCMD(0x04);     
	EPD_W21_WriteDATA(*wavefrom++); 
	EPD_W21_WriteDATA(*wavefrom++);
	EPD_W21_WriteDATA(*wavefrom++);

	EPD_W21_WriteCMD(0x2C);         ///vcom   
	EPD_W21_WriteDATA(*wavefrom++);	
}


void Write_LUT_All(void)
{
	float temp;
		temp = Read_temp();

	if(temp <= 5)
	{
		Write_LUT(WS_0_5); //WS_0_5
	}
	else if(temp <=10) 
	{
		Write_LUT(WS_5_10);//WS_5_10
	}
	else if(temp <=15)
	{
		Write_LUT(WS_10_15);//WS_10_15
	}
	else if(temp <=20)
	{
		Write_LUT(WS_15_20);//WS_15_20
	}
	else
	{
		Write_LUT(WS_20_80);//WS_20_80
	}
	
}
void Write_LUT_Fast(void)
{
	Write_LUT(WS_80_127); 
}

void EPD_Update(void)
{
	Write_LUT_All();

	EPD_W21_WriteCMD(0x22);  
	EPD_W21_WriteDATA(0xC7);   
	EPD_W21_WriteCMD(0x20); 
	Epaper_READBUSY(); 	
}
//Fast update function
void EPD_Update_Fast(void)
{   
  Write_LUT_Fast();
	
	Epaper_READBUSY();  	
  EPD_W21_WriteCMD(0x22); //Display Update Control
  EPD_W21_WriteDATA(0xC7);   
  EPD_W21_WriteCMD(0x20); //Activate Display Update Sequence
  Epaper_READBUSY();   

}

/***********************************************************
						end file
***********************************************************/
