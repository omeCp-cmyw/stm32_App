#ifndef      __BSP_NT35510_LCD_H
#define	     __BSP_NT35510_LCD_H


#include "stm32f4xx_hal.h"
#include "fonts.h"


/***************************************************************************************
2^26 =0X0400 0000 = 64MB,每块 BANK 是4*64MB = 256MB
64MB:FSMC_Bank1_NORSRAM1:0X6000 0000 ~ 0X63FF FFFF
64MB:FSMC_Bank1_NORSRAM2:0X6400 0000 ~ 0X67FF FFFF
64MB:FSMC_Bank1_NORSRAM3:0X6800 0000 ~ 0X6BFF FFFF
64MB:FSMC_Bank1_NORSRAM4:0X6C00 0000 ~ 0X6FFF FFFF

选择BANK1-BORSRAM3 作为 TFT，地址范围为0X6800 0000 ~ 0X6BFF FFFF
FSMC_A0 接LCD的DC(寄存器/数据选择)引脚
寄存器基地址 = 0X6C00 0000
RAM基地址 = 0X6D00 0000 = 0X6C00 0000+2^0*2 = 0X6800 0000 + 0X2 = 0X6800 0002
若选择不同的地址线时，地址要重新计算  
****************************************************************************************/

/******************************* NT35510 显示屏FSMC 地址定义 ***************************/
//FSMC_Bank1_NORSRAM用于LCD命令操作的地址
#define      FSMC_Addr_NT35510_CMD         ( ( uint32_t ) 0x68000000 )

//FSMC_Bank1_NORSRAM用于LCD数据操作的地址      
#define      FSMC_Addr_NT35510_DATA        ( ( uint32_t ) 0x68000002 )

//片选引脚对应的NOR/SRAM块（Bank1-NORSRAM3）
#define      FSMC_Bank1_NORSRAMx           FSMC_NORSRAM_BANK3


/******************************* NT35510 显示屏8080通讯引脚定义 ***************************/
/******控制信号线******/
//片选引脚选择NOR/SRAM块
#define      NT35510_CS_PORT               GPIOG
#define      NT35510_CS_PIN                GPIO_PIN_10

//DC引脚，使用FSMC的地址线控制，这个引脚决定了LCD时使用的地址
//PF0为FSMC_A0
#define      NT35510_DC_PORT               GPIOF
#define      NT35510_DC_PIN                GPIO_PIN_0

//写使能
#define      NT35510_WR_PORT               GPIOD
#define      NT35510_WR_PIN                GPIO_PIN_5

//读使能
#define      NT35510_RD_PORT               GPIOD
#define      NT35510_RD_PIN                GPIO_PIN_4

//复位引脚
#define      NT35510_RST_PORT              GPIOF
#define      NT35510_RST_PIN               GPIO_PIN_11

//背光引脚
#define      NT35510_BK_PORT               GPIOF
#define      NT35510_BK_PIN                GPIO_PIN_9

/********数据信号线***************/
#define      NT35510_D0_PORT               GPIOD
#define      NT35510_D0_PIN                GPIO_PIN_14

#define      NT35510_D1_PORT               GPIOD
#define      NT35510_D1_PIN                GPIO_PIN_15

#define      NT35510_D2_PORT               GPIOD
#define      NT35510_D2_PIN                GPIO_PIN_0

#define      NT35510_D3_PORT               GPIOD
#define      NT35510_D3_PIN                GPIO_PIN_1

#define      NT35510_D4_PORT               GPIOE
#define      NT35510_D4_PIN                GPIO_PIN_7

#define      NT35510_D5_PORT               GPIOE
#define      NT35510_D5_PIN                GPIO_PIN_8

#define      NT35510_D6_PORT               GPIOE
#define      NT35510_D6_PIN                GPIO_PIN_9

#define      NT35510_D7_PORT               GPIOE
#define      NT35510_D7_PIN                GPIO_PIN_10

#define      NT35510_D8_PORT               GPIOE
#define      NT35510_D8_PIN                GPIO_PIN_11

#define      NT35510_D9_PORT               GPIOE
#define      NT35510_D9_PIN                GPIO_PIN_12

#define      NT35510_D10_PORT               GPIOE
#define      NT35510_D10_PIN                GPIO_PIN_13

#define      NT35510_D11_PORT               GPIOE
#define      NT35510_D11_PIN                GPIO_PIN_14

#define      NT35510_D12_PORT               GPIOE
#define      NT35510_D12_PIN                GPIO_PIN_15

#define      NT35510_D13_PORT               GPIOD
#define      NT35510_D13_PIN                GPIO_PIN_8

#define      NT35510_D14_PORT               GPIOD
#define      NT35510_D14_PIN                GPIO_PIN_9

#define      NT35510_D15_PORT               GPIOD
#define      NT35510_D15_PIN                GPIO_PIN_10

/*************************************** 预定义 ******************************************/
#define      DEBUG_DELAY()               Delay(0x5000)

/***************************** NT35510 显示屏参数定义 ***************************/
#define      NT35510_DispWindow_X_Star		    0     //起始坐标X
#define      NT35510_DispWindow_Y_Star		    0     //起始坐标Y

// 4.3寸NT35510分辨率
#define 			NT35510_LESS_PIXEL	  		480			//液晶短边方向像素宽度
#define 			NT35510_MORE_PIXEL	 		800			//液晶长边方向像素宽度

//根据液晶扫描方向而变化的XY宽度
//调用ILI9806G_GramScan函数设置方向时会自动更改
extern uint16_t LCD_X_LENGTH,LCD_Y_LENGTH; 

//液晶扫描模式
//可选值为0-7
extern uint8_t LCD_SCAN_MODE;

/******************************* 定义NT35510 显示屏常用颜色 ********************************/
#define      BACKGROUND		                BLACK   //默认背景颜色

#define      WHITE		 		                  0xFFFF	   //白色
#define      BLACK                         0x0000	   //黑色 
#define      GREY                          0xF7DE	   //灰色 
#define      BLUE                          0x001F	   //蓝色 
#define      BLUE2                         0x051F	   //浅蓝色 
#define      RED                           0xF800	   //红色 
#define      MAGENTA                       0xF81F	   //红紫色，品红色 
#define      GREEN                         0x07E0	   //绿色 
#define      CYAN                          0x7FFF	   //蓝绿色，青色 
#define      YELLOW                        0xFFE0	   //黄色 
#define      BRED                          0xF81F
#define      GRED                          0xFFE0
#define      GBLUE                         0x07FF



/******************************* 定义NT35510 命令 ********************************/
#define      CMD_SetCoordinateX		 		    0x2A00	     //设置X坐标
#define      CMD_SetCoordinateY		 		    0x2B00	     //设置Y坐标
#define      CMD_SetPixel		 		          0x2C00	     //写像素




/********************************** 定义NT35510 函数 ***************************************/
void                     NT35510_Init                    ( void );
void                     NT35510_Rst                     ( void );
void                     NT35510_BackLed_Control         ( FunctionalState enumState );
void                     NT35510_GramScan                ( uint8_t ucOtion );
void                     NT35510_OpenWindow              ( uint16_t usX, uint16_t usY, uint16_t usWidth, uint16_t usHeight );
void                     NT35510_Clear                   ( uint16_t usX, uint16_t usY, uint16_t usWidth, uint16_t usHeight );
void                     NT35510_SetPointPixel           ( uint16_t usX, uint16_t usY );
uint16_t                 NT35510_GetPointPixel           ( uint16_t usX , uint16_t usY );
void                     NT35510_DrawLine                ( uint16_t usX1, uint16_t usY1, uint16_t usX2, uint16_t usY2 );
void                     NT35510_DrawRectangle           ( uint16_t usX_Start, uint16_t usY_Start, uint16_t usWidth, uint16_t usHeight,uint8_t ucFilled );
void                     NT35510_DrawCircle              ( uint16_t usX_Center, uint16_t usY_Center, uint16_t usRadius, uint8_t ucFilled );
void                     NT35510_DispChar_EN             ( uint16_t usX, uint16_t usY, const char cChar );
void                     NT35510_DispStringLine_EN      ( uint16_t line, char * pStr );
void                     NT35510_DispString_EN      			( uint16_t usX, uint16_t usY, char * pStr );
void 											NT35510_DispString_EN_YDir 		(   uint16_t usX,uint16_t usY ,  char * pStr );
void                     NT35510_DispChar_CH             ( uint16_t usX, uint16_t usY, uint16_t usChar );
void                     NT35510_DispString_CH           ( uint16_t usX, uint16_t usY,  char * pStr );
void                     NT35510_DispString_EN_CH        (	uint16_t usX, uint16_t usY,  char * pStr );
void 											NT35510_DispStringLine_EN_CH 	(  uint16_t line, char * pStr );
void 											NT35510_DispString_EN_YDir 		(   uint16_t usX,uint16_t usY ,  char * pStr );
void 											NT35510_DispString_EN_CH_YDir 	(   uint16_t usX,uint16_t usY , char * pStr );

void 											LCD_SetFont											(sFONT *fonts);
sFONT 										*LCD_GetFont											(void);
void 											NT35510_ClearLine										(uint16_t Line);
void 											LCD_SetBackColor								(uint16_t Color);
void 											LCD_SetTextColor								(uint16_t Color)	;
void 											LCD_SetColors										(uint16_t TextColor, uint16_t BackColor);
void 											LCD_GetColors										(uint16_t *TextColor, uint16_t *BackColor);

#define 									LCD_ClearLine 						NT35510_ClearLine

void NT35510_DisplayStringEx(uint16_t x, 		//字符显示位置x
																 uint16_t y, 				//字符显示位置y
																 uint16_t Font_width,	//要显示的字体宽度，英文字符在此基础上减半,注意为偶数
																 uint16_t Font_Height,	//要显示的字体高度，注意为偶数
																 uint8_t *ptr,					//显示的字符
																 uint16_t DrawModel);  //是否反色显示

void NT35510_DisplayStringEx_YDir(uint16_t x, 		//字符显示位置x
																			 uint16_t y, 				//字符显示位置y
																			 uint16_t Font_width,	//要显示的字体宽度，英文字符在此基础上减半,注意为偶数
																			 uint16_t Font_Height,	//要显示的字体高度，注意为偶数
																			 uint8_t *ptr,					//显示的字符
																			 uint16_t DrawModel);  //是否反色显示


#endif /* __BSP_NT35510_H */
