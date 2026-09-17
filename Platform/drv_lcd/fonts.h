#ifndef __FONT_H
#define __FONT_H       

#include "stm32f4xx_hal.h"



/** @defgroup FONTS_Exported_Types
  * @{
  */ 
typedef struct _tFont
{    
  const uint8_t *table;
  uint16_t Width;
  uint16_t Height;
  
} sFONT;

extern sFONT Font24x48;
extern sFONT Font16x32;
extern sFONT Font8x16;


/*******************中文********** 在显示屏上显示的字符大小 ***************************/
#define      WIDTH_CH_CHAR		                32	    //中文字符宽度 
#define      HEIGHT_CH_CHAR		              	32		  //中文字符高度 



#define LINE(x) ((x) * (((sFONT *)LCD_GetFont())->Height))

//LINEY统一使用汉字字模的高度
#define LINEY(x) ((x) * (WIDTH_CH_CHAR))




//中文字库暂未接入，GetGBKCode为桩函数（仅支持英文显示）
int GetGBKCode ( uint8_t * pBuffer, uint16_t c );


#endif /*end of __FONT_H    */
