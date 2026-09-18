/********************************************************************************
**
** 文件名:     bsp_nt35510_lcd.c
** 版权所有:   无
** 文件描述:   NT35510 LCD驱动
**
*********************************************************************************/

#include "bsp_nt35510_lcd.h"
#include "fonts.h"
#include <stdio.h>

/* 定义LCD驱动的全局变量 */
uint16_t LCD_X_LENGTH = NT35510_LESS_PIXEL;
uint16_t LCD_Y_LENGTH = NT35510_MORE_PIXEL;
uint8_t LCD_SCAN_MODE = 6;

/* 记录当前字体和颜色 */
static sFONT *LCD_Currentfonts;
static uint16_t CurrentTextColor   = 0xFFFF;
static uint16_t CurrentBackColor   = 0x0000;

/* 注：NT35510_DispWindow_X_Star/Y_Star 在头文件中定义为宏 0 */

/* 用于存储中文字符的缓冲区 */
uint8_t ucBuffer [ WIDTH_CH_CHAR*HEIGHT_CH_CHAR/8 ];


/**
  * @brief  简单延时函数
  * @param  nCount 延时计数值
  * @retval 无
  */
static void Delay ( __IO uint32_t nCount )
{
  for ( ; nCount != 0; nCount -- );
}


/**
  * @brief  向NT35510写入命令
  * @param  usCmd :要写入的命令（寄存器地址）
  * @retval 无
  */
void NT35510_Write_Cmd ( uint16_t usCmd )
{
	* ( __IO uint16_t * ) ( FSMC_Addr_NT35510_CMD ) = usCmd;
}


/**
  * @brief  向NT35510写入数据
  * @param  usData :要写入的数据
  * @retval 无
  */
void NT35510_Write_Data ( uint16_t usData )
{
	* ( __IO uint16_t * ) ( FSMC_Addr_NT35510_DATA ) = usData;
}


/**
  * @brief  从NT35510读取数据
  * @param  无
  * @retval 读取到的数据
  */
uint16_t NT35510_Read_Data ( void )
{
	return ( * ( __IO uint16_t * ) ( FSMC_Addr_NT35510_DATA ) );
}


/**
  * @brief  NT35510复位
  * @param  无
  * @retval 无
  */
void NT35510_Rst ( void )
{
	HAL_GPIO_WritePin ( NT35510_RST_PORT, NT35510_RST_PIN, GPIO_PIN_RESET );	 //低电平复位
	Delay ( 0xFFFFF ); 					 						   
	HAL_GPIO_WritePin ( NT35510_RST_PORT, NT35510_RST_PIN, GPIO_PIN_SET );	 	 
	Delay ( 0x1FFFFF ); 	
	
}


/**
  * @brief  初始化NT35510的GPIO引脚
  * @param  无
  * @retval 无
  */
static void NT35510_GPIO_Config ( void )
{
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 使能FMC及对应GPIO的时钟*/
	__HAL_RCC_GPIOB_CLK_ENABLE ();
	__HAL_RCC_GPIOC_CLK_ENABLE ();
	__HAL_RCC_GPIOD_CLK_ENABLE ();
	__HAL_RCC_GPIOE_CLK_ENABLE ();
	__HAL_RCC_GPIOF_CLK_ENABLE ();
	__HAL_RCC_GPIOG_CLK_ENABLE ();

	/* 配置FSMC相对应的数据线,FSMC-D0~D15及控制线（复用推挽，上拉，100MHz）*/
	GPIO_InitStructure.Mode  = GPIO_MODE_AF_PP;
	GPIO_InitStructure.Pull  = GPIO_PULLUP;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

	/* PD14,PD15,PD0,PD1,PD8,PD9,PD10：数据线D0-D3、D13-D15 */
	GPIO_InitStructure.Pin = NT35510_D0_PIN|NT35510_D1_PIN|NT35510_D2_PIN|
							 NT35510_D3_PIN|NT35510_D13_PIN|NT35510_D14_PIN|NT35510_D15_PIN;
	GPIO_InitStructure.Alternate = GPIO_AF12_FSMC;
	HAL_GPIO_Init ( NT35510_D0_PORT, & GPIO_InitStructure );

	/* PE7-PE15：数据线D4-D12 */
	GPIO_InitStructure.Pin = NT35510_D4_PIN|NT35510_D5_PIN|NT35510_D6_PIN|NT35510_D7_PIN|
							 NT35510_D8_PIN|NT35510_D9_PIN|NT35510_D10_PIN|NT35510_D11_PIN|NT35510_D12_PIN;
	GPIO_InitStructure.Alternate = GPIO_AF12_FSMC;
	HAL_GPIO_Init ( NT35510_D4_PORT, & GPIO_InitStructure );

	/* PD4(RD)、PD5(WR)：控制线 */
	GPIO_InitStructure.Pin = NT35510_RD_PIN|NT35510_WR_PIN;
	GPIO_InitStructure.Alternate = GPIO_AF12_FSMC;
	HAL_GPIO_Init ( NT35510_RD_PORT, & GPIO_InitStructure );

	/* PF0(DC=FSMC_A0)：控制线 */
	GPIO_InitStructure.Pin = NT35510_DC_PIN;
	GPIO_InitStructure.Alternate = GPIO_AF12_FSMC;
	HAL_GPIO_Init ( NT35510_DC_PORT, & GPIO_InitStructure );

	/* PG10(CS=FSMC_NE3)：控制线 */
	GPIO_InitStructure.Pin = NT35510_CS_PIN;
	GPIO_InitStructure.Alternate = GPIO_AF12_FSMC;
	HAL_GPIO_Init ( NT35510_CS_PORT, & GPIO_InitStructure );

	/* 配置LCD复位RST控制管脚*/
	GPIO_InitStructure.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Pull  = GPIO_PULLUP;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;

	GPIO_InitStructure.Pin = NT35510_RST_PIN;
	HAL_GPIO_Init ( NT35510_RST_PORT, & GPIO_InitStructure );

	/* 配置LCD背光控制管脚BK*/
	GPIO_InitStructure.Pin = NT35510_BK_PIN;
	HAL_GPIO_Init ( NT35510_BK_PORT, & GPIO_InitStructure );

}


 /**
  * @brief  LCD  FSMC 模式配置
  * @param  无
  * @retval 无
  */
static void NT35510_FSMC_Config ( void )
{
	SRAM_HandleTypeDef           hsram;
	FSMC_NORSRAM_TimingTypeDef     readWriteTiming;

	/* 使能FMC时钟*/
	__HAL_RCC_FSMC_CLK_ENABLE ();

	hsram.Instance  = FSMC_NORSRAM_DEVICE;
	hsram.Extended  = FSMC_NORSRAM_EXTENDED_DEVICE;

	/* 地址建立4个HCLK，数据建立11个HCLK，模式B */
	readWriteTiming.AddressSetupTime       = 0x04;	 //地址建立时间 5/168M=30ns
	readWriteTiming.AddressHoldTime        = 0x00;	 //地址保持时间
	readWriteTiming.DataSetupTime          = 0x0b;	 //数据建立时间 12/168M=72ns
	readWriteTiming.BusTurnAroundDuration  = 0x00;
	readWriteTiming.CLKDivision            = 0x00;
	readWriteTiming.DataLatency            = 0x00;
	readWriteTiming.AccessMode             = FSMC_ACCESS_MODE_B;

	hsram.Init.NSBank             = FSMC_Bank1_NORSRAMx;
	hsram.Init.DataAddressMux     = FSMC_DATA_ADDRESS_MUX_DISABLE;
	hsram.Init.MemoryType         = FSMC_MEMORY_TYPE_NOR;
	hsram.Init.MemoryDataWidth    = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
	hsram.Init.BurstAccessMode    = FSMC_BURST_ACCESS_MODE_DISABLE;
	hsram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
	hsram.Init.WrapMode           = FSMC_WRAP_MODE_DISABLE;
	hsram.Init.WaitSignalActive   = FSMC_WAIT_TIMING_BEFORE_WS;
	hsram.Init.WriteOperation     = FSMC_WRITE_OPERATION_ENABLE;
	hsram.Init.WaitSignal         = FSMC_WAIT_SIGNAL_DISABLE;
	hsram.Init.ExtendedMode       = FSMC_EXTENDED_MODE_DISABLE;
	hsram.Init.AsynchronousWait   = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
	hsram.Init.WriteBurst         = FSMC_WRITE_BURST_DISABLE;
	hsram.Init.ContinuousClock    = FSMC_CONTINUOUS_CLOCK_SYNC_ONLY;

	/* 初始化FMC Bank1-NORSRAM3，并使能 */
	HAL_SRAM_Init ( & hsram, & readWriteTiming, NULL );

}


/**
  * @brief  初始化NT35510寄存器
  * @param  无
  * @retval 无
  */
static void NT35510_REG_Config ( void )
{	
	printf("[LCD] reg1 rst...\r\n");
	//NT35510初始化序列
	// 1. 硬件复位
	NT35510_Rst();
	printf("[LCD] reg1 rst done\r\n");
	printf("[LCD] BCR3=0x%08X BTR3=0x%08X\r\n",
	       (unsigned)FSMC_Bank1->BTCR[6], (unsigned)FSMC_Bank1->BTCR[7]);
	
	// 2. 发送初始化序列（NT35510-HSD43 4.3寸序列）
	///NT35510-HSD43
	//PAGE1
	NT35510_Write_Cmd(0xF000);    NT35510_Write_Data(0x0055);
	printf("[LCD] first write ok\r\n");
	NT35510_Write_Cmd(0xF001);    NT35510_Write_Data(0x00AA);
	NT35510_Write_Cmd(0xF002);    NT35510_Write_Data(0x0052);
	NT35510_Write_Cmd(0xF003);    NT35510_Write_Data(0x0008);
	NT35510_Write_Cmd(0xF004);    NT35510_Write_Data(0x0001);

	//Set AVDD 5.2V
	NT35510_Write_Cmd(0xB000);    NT35510_Write_Data(0x000D);
	NT35510_Write_Cmd(0xB001);    NT35510_Write_Data(0x000D);
	NT35510_Write_Cmd(0xB002);    NT35510_Write_Data(0x000D);

	//Set AVEE 5.2V
	NT35510_Write_Cmd(0xB100);    NT35510_Write_Data(0x000D);
	NT35510_Write_Cmd(0xB101);    NT35510_Write_Data(0x000D);
	NT35510_Write_Cmd(0xB102);    NT35510_Write_Data(0x000D);

	//Set VCL -2.5V
	NT35510_Write_Cmd(0xB200);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xB201);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xB202);    NT35510_Write_Data(0x0000);				

	//Set AVDD Ratio
	NT35510_Write_Cmd(0xB600);    NT35510_Write_Data(0x0044);
	NT35510_Write_Cmd(0xB601);    NT35510_Write_Data(0x0044);
	NT35510_Write_Cmd(0xB602);    NT35510_Write_Data(0x0044);

	//Set AVEE Ratio
	NT35510_Write_Cmd(0xB700);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB701);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB702);    NT35510_Write_Data(0x0034);

	//Set VCL Ratio
	NT35510_Write_Cmd(0xB800);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB801);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB802);    NT35510_Write_Data(0x0034);
		
	//Control VGH booster voltage rang
	NT35510_Write_Cmd(0xBF00);    NT35510_Write_Data(0x0001); //VGH:7~18V	

	//VGH=15V(1V/step) Free pump
	NT35510_Write_Cmd(0xB300);    NT35510_Write_Data(0x000f);
	NT35510_Write_Cmd(0xB301);    NT35510_Write_Data(0x000f);
	NT35510_Write_Cmd(0xB302);    NT35510_Write_Data(0x000f);

	//VGH Ratio
	NT35510_Write_Cmd(0xB900);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB901);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xB902);    NT35510_Write_Data(0x0034);

	//VGL_REG=-10(1V/step)
	NT35510_Write_Cmd(0xB500);    NT35510_Write_Data(0x0008);
	NT35510_Write_Cmd(0xB501);    NT35510_Write_Data(0x0008);
	NT35510_Write_Cmd(0xB502);    NT35510_Write_Data(0x0008);

	NT35510_Write_Cmd(0xC200);    NT35510_Write_Data(0x0003);

	//VGLX Ratio
	NT35510_Write_Cmd(0xBA00);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xBA01);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xBA02);    NT35510_Write_Data(0x0034);

	//VGMP/VGSP=4.5V/0V
	NT35510_Write_Cmd(0xBC00);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xBC01);    NT35510_Write_Data(0x0078);
	NT35510_Write_Cmd(0xBC02);    NT35510_Write_Data(0x0000);

	//VGMN/VGSN=-4.5V/0V
	NT35510_Write_Cmd(0xBD00);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xBD01);    NT35510_Write_Data(0x0078);
	NT35510_Write_Cmd(0xBD02);    NT35510_Write_Data(0x0000);

	//Vcom=-1.4V(12.5mV/step)
	NT35510_Write_Cmd(0xBE00);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xBE01);    NT35510_Write_Data(0x0064);

	//Gamma (R+)
	NT35510_Write_Cmd(0xD100);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD101);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD102);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD103);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD104);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD105);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD106);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD107);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD108);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD109);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD10A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD10B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD10C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD10D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD10E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD10F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD110);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD111);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD112);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD113);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD114);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD115);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD116);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD117);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD118);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD119);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD11A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD11B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD11C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD11D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD11E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD11F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD120);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD121);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD122);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD123);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD124);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD125);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD126);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD127);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD128);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD129);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD12A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD12B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD12C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD12D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD12E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD12F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD130);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD131);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD132);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD133);    NT35510_Write_Data(0x006D);

	//Gamma (G+)
	NT35510_Write_Cmd(0xD200);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD201);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD202);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD203);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD204);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD205);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD206);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD207);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD208);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD209);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD20A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD20B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD20C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD20D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD20E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD20F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD210);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD211);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD212);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD213);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD214);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD215);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD216);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD217);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD218);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD219);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD21A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD21B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD21C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD21D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD21E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD21F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD220);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD221);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD222);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD223);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD224);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD225);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD226);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD227);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD228);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD229);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD22A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD22B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD22C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD22D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD22E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD22F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD230);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD231);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD232);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD233);    NT35510_Write_Data(0x006D);

	//Gamma (B+)
	NT35510_Write_Cmd(0xD300);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD301);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD302);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD303);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD304);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD305);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD306);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD307);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD308);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD309);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD30A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD30B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD30C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD30D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD30E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD30F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD310);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD311);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD312);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD313);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD314);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD315);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD316);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD317);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD318);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD319);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD31A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD31B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD31C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD31D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD31E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD31F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD320);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD321);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD322);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD323);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD324);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD325);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD326);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD327);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD328);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD329);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD32A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD32B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD32C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD32D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD32E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD32F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD330);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD331);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD332);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD333);    NT35510_Write_Data(0x006D);

	//Gamma (R-)
	NT35510_Write_Cmd(0xD400);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD401);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD402);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD403);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD404);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD405);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD406);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD407);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD408);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD409);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD40A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD40B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD40C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD40D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD40E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD40F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD410);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD411);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD412);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD413);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD414);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD415);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD416);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD417);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD418);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD419);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD41A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD41B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD41C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD41D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD41E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD41F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD420);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD421);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD422);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD423);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD424);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD425);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD426);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD427);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD428);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD429);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD42A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD42B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD42C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD42D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD42E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD42F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD430);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD431);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD432);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD433);    NT35510_Write_Data(0x006D);

	//Gamma (G-)
	NT35510_Write_Cmd(0xD500);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD501);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD502);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD503);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD504);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD505);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD506);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD507);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD508);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD509);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD50A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD50B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD50C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD50D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD50E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD50F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD510);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD511);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD512);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD513);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD514);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD515);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD516);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD517);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD518);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD519);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD51A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD51B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD51C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD51D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD51E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD51F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD520);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD521);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD522);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD523);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD524);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD525);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD526);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD527);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD528);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD529);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD52A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD52B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD52C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD52D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD52E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD52F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD530);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD531);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD532);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD533);    NT35510_Write_Data(0x006D);

	//Gamma (B-)
	NT35510_Write_Cmd(0xD600);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD601);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD602);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD603);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD604);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD605);    NT35510_Write_Data(0x003A);
	NT35510_Write_Cmd(0xD606);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD607);    NT35510_Write_Data(0x004A);
	NT35510_Write_Cmd(0xD608);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD609);    NT35510_Write_Data(0x005C);
	NT35510_Write_Cmd(0xD60A);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD60B);    NT35510_Write_Data(0x0081);
	NT35510_Write_Cmd(0xD60C);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD60D);    NT35510_Write_Data(0x00A6);
	NT35510_Write_Cmd(0xD60E);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD60F);    NT35510_Write_Data(0x00E5);
	NT35510_Write_Cmd(0xD610);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD611);    NT35510_Write_Data(0x0013);
	NT35510_Write_Cmd(0xD612);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD613);    NT35510_Write_Data(0x0054);
	NT35510_Write_Cmd(0xD614);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD615);    NT35510_Write_Data(0x0082);
	NT35510_Write_Cmd(0xD616);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD617);    NT35510_Write_Data(0x00CA);
	NT35510_Write_Cmd(0xD618);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD619);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xD61A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD61B);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xD61C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD61D);    NT35510_Write_Data(0x0034);
	NT35510_Write_Cmd(0xD61E);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD61F);    NT35510_Write_Data(0x0067);
	NT35510_Write_Cmd(0xD620);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD621);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xD622);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD623);    NT35510_Write_Data(0x00A4);
	NT35510_Write_Cmd(0xD624);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD625);    NT35510_Write_Data(0x00B7);
	NT35510_Write_Cmd(0xD626);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD627);    NT35510_Write_Data(0x00CF);
	NT35510_Write_Cmd(0xD628);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD629);    NT35510_Write_Data(0x00DE);
	NT35510_Write_Cmd(0xD62A);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD62B);    NT35510_Write_Data(0x00F2);
	NT35510_Write_Cmd(0xD62C);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xD62D);    NT35510_Write_Data(0x00FE);
	NT35510_Write_Cmd(0xD62E);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD62F);    NT35510_Write_Data(0x0010);
	NT35510_Write_Cmd(0xD630);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD631);    NT35510_Write_Data(0x0033);
	NT35510_Write_Cmd(0xD632);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xD633);    NT35510_Write_Data(0x006D);

	//PAGE0
	NT35510_Write_Cmd(0xF000);    NT35510_Write_Data(0x0055);
	NT35510_Write_Cmd(0xF001);    NT35510_Write_Data(0x00AA);
	NT35510_Write_Cmd(0xF002);    NT35510_Write_Data(0x0052);
	NT35510_Write_Cmd(0xF003);    NT35510_Write_Data(0x0008);	
	NT35510_Write_Cmd(0xF004);    NT35510_Write_Data(0x0000); 

	//480x800
	NT35510_Write_Cmd(0xB500);    NT35510_Write_Data(0x0050);

	//Dispay control
	NT35510_Write_Cmd(0xB100);    NT35510_Write_Data(0x00CC);	
	NT35510_Write_Cmd(0xB101);    NT35510_Write_Data(0x0000); // S1->S1440:00;S1440->S1:02

	//Source hold time (Nova non-used)
	NT35510_Write_Cmd(0xB600);    NT35510_Write_Data(0x0005);

	//Gate EQ control	 (Nova non-used)
	NT35510_Write_Cmd(0xB700);    NT35510_Write_Data(0x0077);	 
	NT35510_Write_Cmd(0xB701);    NT35510_Write_Data(0x0077);

	//Source EQ control (Nova non-used)
	NT35510_Write_Cmd(0xB800);    NT35510_Write_Data(0x0001);  
	NT35510_Write_Cmd(0xB801);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xB802);    NT35510_Write_Data(0x0003);
	NT35510_Write_Cmd(0xB803);    NT35510_Write_Data(0x0003);

	//Inversion mode: column
	NT35510_Write_Cmd(0xBC00);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xBC01);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0xBC02);    NT35510_Write_Data(0x0000); 

	//Frame rate	(Nova non-used)
	NT35510_Write_Cmd(0xBD00);    NT35510_Write_Data(0x0001);
	NT35510_Write_Cmd(0xBD01);    NT35510_Write_Data(0x0084);
	NT35510_Write_Cmd(0xBD02);    NT35510_Write_Data(0x001c);
	NT35510_Write_Cmd(0xBD03);    NT35510_Write_Data(0x001c);
	NT35510_Write_Cmd(0xBD04);    NT35510_Write_Data(0x0000);

	//LGD timing control(4H/4-delay_ms)
	NT35510_Write_Cmd(0xC900);    NT35510_Write_Data(0x00D0);
	NT35510_Write_Cmd(0xC901);    NT35510_Write_Data(0x0002);
	NT35510_Write_Cmd(0xC902);    NT35510_Write_Data(0x0050);
	NT35510_Write_Cmd(0xC903);    NT35510_Write_Data(0x0050);	/* STV delay_ms time */
	NT35510_Write_Cmd(0xC904);    NT35510_Write_Data(0x0050);	/* CLK delay_ms time */

	NT35510_Write_Cmd(0x3600);    NT35510_Write_Data(0x0000);
	NT35510_Write_Cmd(0x3500);    NT35510_Write_Data(0x0000);

	NT35510_Write_Cmd(0xFF00);    NT35510_Write_Data(0x00AA);
	NT35510_Write_Cmd(0xFF01);    NT35510_Write_Data(0x0055);
	NT35510_Write_Cmd(0xFF02);    NT35510_Write_Data(0x0025);
	NT35510_Write_Cmd(0xFF03);    NT35510_Write_Data(0x0001);

	NT35510_Write_Cmd(0xFC00);    NT35510_Write_Data(0x0016);
	NT35510_Write_Cmd(0xFC01);    NT35510_Write_Data(0x00A2);
	NT35510_Write_Cmd(0xFC02);    NT35510_Write_Data(0x0026);
	NT35510_Write_Cmd(0x3A00);    NT35510_Write_Data(0x0006);

	NT35510_Write_Cmd(0x3A00);    NT35510_Write_Data(0x0055);
	printf("[LCD] reg2 seq done\r\n");
	//Sleep out
	NT35510_Write_Cmd(0x1100);
	Delay(0xFFFFFF);
	printf("[LCD] reg3 sleep-out done\r\n");

	//Display on
	NT35510_Write_Cmd(0x2900);
	printf("[LCD] reg4 display-on done\r\n");
	
	// 3. 设置默认扫描方向
	NT35510_GramScan(LCD_SCAN_MODE);
	
	// 4. 清屏
	NT35510_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
	
	// 5. 开启背光
	NT35510_BackLed_Control(ENABLE);
}


/**
  * @brief  NT35510初始化
  * @param  无
  * @retval 无
  */
void NT35510_Init ( void )
{
	printf("[LCD] step1 GPIO config...\r\n");
	NT35510_GPIO_Config ();
	printf("[LCD] step2 FSMC config...\r\n");
	NT35510_FSMC_Config ();
	printf("[LCD] step3 REG config...\r\n");
	NT35510_REG_Config ();
	printf("[LCD] step4 scan/clear...\r\n");
	
	//设置默认扫描方向，其中6模式为大部分液晶例程的默认显示方向  
	NT35510_GramScan(LCD_SCAN_MODE);
    
	NT35510_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);	/* 清屏，显示全黑 */
	NT35510_BackLed_Control ( ENABLE );      //点亮LCD背光
	printf("[LCD] step5 init done\r\n");
}


/**
  * @brief  设置LCD的GRAM的扫描方向
  * @param  ucOption: 选择GRAM的扫描方向 
  *     参数可选值：
  *     @arg 0-7 :参数可选值为0-7这8个方向
  *		
  *	@brief 	！！！其中0、6为横屏，1、7为竖屏，并由LCD_SCAN_MODE宏定义配置
  *	
  * @note 一般使用6模式(横屏，从左到右，从上到下) 
  *			液晶屏的默认显示方向由LCD_SCAN_MODE的宏定义配置
  * @retval 无
  */
void NT35510_GramScan ( uint8_t ucOption )
{	
	//参数检查，只接受0-7
	if(ucOption >7 )
		return;
	
	//根据模式更新LCD_SCAN_MODE的值，主要用于触摸屏选择计算参数
	LCD_SCAN_MODE = ucOption;
	
	//根据模式更新XY方向的宽度
	if(ucOption%2 == 0)	
	{
		//0 2 4 6模式下，X方向像素宽度为480，Y方向为800
		LCD_X_LENGTH = NT35510_LESS_PIXEL;
		LCD_Y_LENGTH =	NT35510_MORE_PIXEL;
	}
	else				
	{
		//1 3 5 7模式下，X方向像素宽度为800，Y方向为480
		LCD_X_LENGTH = NT35510_MORE_PIXEL;
		LCD_Y_LENGTH =	NT35510_LESS_PIXEL; 
	}

	//0x36命令参数的高3位可用于设置GRAM扫描方向	
	NT35510_Write_Cmd ( 0x3600 ); 
	NT35510_Write_Data (0x00 | (ucOption<<5));//根据ucOption的值设置LCD的GRAM扫描方向
	NT35510_Write_Cmd ( CMD_SetCoordinateX ); 
	NT35510_Write_Data ( 0x00 );		/* x start high byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 1 ); 
	NT35510_Write_Data ( 0x00 );		/* x start low byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 2 ); 
	NT35510_Write_Data ( ((LCD_X_LENGTH-1)>>8)&0xFF ); /* x end high byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 3 ); 
	NT35510_Write_Data ( (LCD_X_LENGTH-1)&0xFF );			/* x end low byte */

	NT35510_Write_Cmd ( CMD_SetCoordinateY ); 
	NT35510_Write_Data ( 0x00 );		/* y start high byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 1 ); 
	NT35510_Write_Data ( 0x00 );		/* y start low byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 2 ); 
	NT35510_Write_Data ( ((LCD_Y_LENGTH-1)>>8)&0xFF );	/* y end high byte */
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 3 ); 
	NT35510_Write_Data ( (LCD_Y_LENGTH-1)&0xFF );			/* y end low byte */

	/* write gram start */
	NT35510_Write_Cmd ( CMD_SetPixel );	
}


/**
  * @brief  在NT35510显示器上开辟一个窗口
  * @param  usX :在特定扫描方向下该窗口的起点X坐标
  * @param  usY :在特定扫描方向下该窗口的起点Y坐标
  * @param  usWidth :窗口的宽度
  * @param  usHeight :窗口的高度
  * @retval 无
  */
void NT35510_OpenWindow ( uint16_t usX, uint16_t usY, uint16_t usWidth, uint16_t usHeight )
{	
	NT35510_Write_Cmd ( CMD_SetCoordinateX );								/* set X coordinate */
	NT35510_Write_Data ( usX >> 8  );	 /* high 8 bits first, then low 8 bits */
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 1 ); 
	NT35510_Write_Data ( usX & 0xff  );	 /* set start and end coordinate */
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 2 ); 
	NT35510_Write_Data ( ( usX + usWidth - 1 ) >> 8  );
	NT35510_Write_Cmd ( CMD_SetCoordinateX + 3 ); 
	NT35510_Write_Data ( ( usX + usWidth - 1 ) & 0xff  );

	NT35510_Write_Cmd ( CMD_SetCoordinateY );							/* set Y coordinate */
	NT35510_Write_Data ( usY >> 8  );
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 1 ); 
	NT35510_Write_Data ( usY & 0xff  );
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 2 ); 
	NT35510_Write_Data ( ( usY + usHeight - 1 ) >> 8 );
	NT35510_Write_Cmd ( CMD_SetCoordinateY + 3 ); 
	NT35510_Write_Data ( ( usY + usHeight - 1) & 0xff );
	
}


/**
  * @brief  设定NT35510的光标坐标
  * @param  usX ：在特定扫描方向下光标的X坐标
  * @param  usY ：在特定扫描方向下光标的Y坐标
  * @retval 无
  */
static void NT35510_SetCursor ( uint16_t usX, uint16_t usY )	
{
	NT35510_OpenWindow ( usX, usY, 1, 1 );
}


/**
  * @brief  在NT35510显示器上以某一颜色填充像素点
  * @param  ulAmout_Point ：要填充颜色的像素点的总数目
  * @param  usColor ：颜色
  * @retval 无
  */
static void NT35510_FillColor ( uint32_t ulAmout_Point, uint16_t usColor )
{	
	uint32_t i = 0;
	
	
	/* memory write */
	NT35510_Write_Cmd ( CMD_SetPixel );	
		
	for ( i = 0; i < ulAmout_Point; i ++ )
		NT35510_Write_Data ( usColor );
		
	
}


/**
  * @brief  对NT35510显示器的某一窗口以某种颜色进行清屏
  * @param  usX :在特定扫描方向下该窗口的起点X坐标
  * @param  usY :在特定扫描方向下该窗口的起点Y坐标
  * @param  usWidth :窗口的宽度
  * @param  usHeight:窗口的高度
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_Clear ( uint16_t usX, uint16_t usY, uint16_t usWidth, uint16_t usHeight )
{
	NT35510_OpenWindow ( usX, usY, usWidth, usHeight );

	NT35510_FillColor ( usWidth * usHeight, CurrentBackColor );		
	
}


/**
  * @brief  对NT35510显示器的某一点以某种颜色进行填充
  * @param  usX :在特定扫描方向下该点的X坐标
  * @param  usY :在特定扫描方向下该点的Y坐标
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_SetPointPixel ( uint16_t usX, uint16_t usY )
{	
	NT35510_SetCursor ( usX, usY );
	
	NT35510_FillColor ( 1, CurrentTextColor );
	
}


/**
  * @brief  读取NT35510显示器某一坐标点的颜色值
  * @param  usX :在特定扫描方向下该点的X坐标
  * @param  usY :在特定扫描方向下该点的Y坐标
  * @retval 颜色值（RGB565）
  */
uint16_t NT35510_GetPointPixel ( uint16_t usX, uint16_t usY )
{ 
	uint16_t usR=0, usG=0, usB=0 ;

	
	NT35510_SetCursor ( usX, usY );
	
	NT35510_Write_Cmd ( 0x2E );   /* 读数据 */
	
	usR = NT35510_Read_Data (); 	/*FIRST READ OUT DUMMY DATA*/
	
	usR = NT35510_Read_Data ();  	/*READ OUT RED DATA  */
	usB = NT35510_Read_Data ();  	/*READ OUT BLUE DATA*/
	usG = NT35510_Read_Data ();  	/*READ OUT GREEN DATA*/	
	
  return ( ( ( usR >> 11 ) << 11 ) | ( ( usG >> 10 ) << 5 ) | ( usB >> 11 ) );
	
}


/**
  * @brief  在NT35510显示器上使用Bresenham算法画线段
  * @param  usX1 :在特定扫描方向下线段的一个端点X坐标
  * @param  usY1 :在特定扫描方向下线段的一个端点Y坐标
  * @param  usX2 :在特定扫描方向下线段的另一个端点X坐标
  * @param  usY2 :在特定扫描方向下线段的另一个端点Y坐标
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DrawLine ( uint16_t usX1, uint16_t usY1, uint16_t usX2, uint16_t usY2 )
{
	uint16_t us; 
	uint16_t usX_Current, usY_Current;
	
	int32_t lError_X = 0, lError_Y = 0, lDelta_X, lDelta_Y, lDistance; 
	int32_t lIncrease_X, lIncrease_Y; 	
	
	
	lDelta_X = usX2 - usX1; //计算坐标增量 
	lDelta_Y = usY2 - usY1; 
	
	usX_Current = usX1; 
	usY_Current = usY1; 
	
	
	if ( lDelta_X > 0 ) 
		lIncrease_X = 1; //设置单步方向 
	else if ( lDelta_X == 0 ) 
		lIncrease_X = 0;//垂直线 
	else 
	{ 
		lIncrease_X = -1;
		lDelta_X = - lDelta_X;
	} 

	
	if ( lDelta_Y > 0 )
		lIncrease_Y = 1; 
	else if ( lDelta_Y == 0 )
		lIncrease_Y = 0;//水平线 
	else 
	{
		lIncrease_Y = -1;
		lDelta_Y = - lDelta_Y;
	} 

	
	if ( lDelta_X > lDelta_Y )
		lDistance = lDelta_X; //选取基本增量坐标轴 
	else 
		lDistance = lDelta_Y; 

	
	for ( us = 0; us <= lDistance + 1; us ++ )//画线输出 
	{  
		NT35510_SetPointPixel ( usX_Current, usY_Current );//画点 
		
		lError_X += lDelta_X ; 
		lError_Y += lDelta_Y ; 
		
		if ( lError_X > lDistance ) 
		{ 
			lError_X -= lDistance; 
			usX_Current += lIncrease_X; 
		}  
		
		if ( lError_Y > lDistance ) 
		{ 
			lError_Y -= lDistance; 
			usY_Current += lIncrease_Y; 
		} 
		
	}  
	
	
}   


/**
  * @brief  在NT35510显示器上画一个矩形
  * @param  usX_Start:在特定扫描方向下矩形的起始点X坐标
  * @param  usY_Start:在特定扫描方向下矩形的起始点Y坐标
  * @param  usWidth:矩形的宽度（单位：像素）
  * @param  usHeight:矩形的高度（单位：像素）
  * @param  ucFilled ：选择是否填充该矩形
  *   @arg 0 :空心矩形
  *   @arg 1 :实心矩形
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DrawRectangle ( uint16_t usX_Start, uint16_t usY_Start, uint16_t usWidth, uint16_t usHeight, uint8_t ucFilled )
{
	if ( ucFilled )
	{
		NT35510_OpenWindow ( usX_Start, usY_Start, usWidth, usHeight );
		NT35510_FillColor ( usWidth * usHeight, CurrentTextColor );	
	}
	else
	{
		NT35510_DrawLine ( usX_Start, usY_Start, usX_Start + usWidth - 1, usY_Start );
		NT35510_DrawLine ( usX_Start, usY_Start + usHeight - 1, usX_Start + usWidth - 1, usY_Start + usHeight - 1 );
		NT35510_DrawLine ( usX_Start, usY_Start, usX_Start, usY_Start + usHeight - 1 );
		NT35510_DrawLine ( usX_Start + usWidth - 1, usY_Start, usX_Start + usWidth - 1, usY_Start + usHeight - 1 );		
	}

}


/**
  * @brief  在NT35510显示器上使用中点画圆算法画一个圆
  * @param  usX_Center :在特定扫描方向下圆心的X坐标
  * @param  usY_Center :在特定扫描方向下圆心的Y坐标
  * @param  usRadius:圆的半径（单位：像素）
  * @param  ucFilled :选择是否填充该圆
  *   @arg 0 :空心圆
  *   @arg 1 :实心圆
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DrawCircle ( uint16_t usX_Center, uint16_t usY_Center, uint16_t usRadius, uint8_t ucFilled )
{
	int16_t sCurrentX, sCurrentY;
	int16_t sError;
	
	
	sCurrentX = 0; sCurrentY = usRadius;	  
	
	sError = 3 - ( usRadius << 1 );     //判断下个点位置的标志
	
	
	while ( sCurrentX <= sCurrentY )
	{
		if ( ucFilled ) 			
			NT35510_DrawLine ( usX_Center - sCurrentY, usY_Center + sCurrentX, usX_Center + sCurrentY, usY_Center + sCurrentX );
		else 		
		{  
			NT35510_SetPointPixel ( usX_Center + sCurrentX, usY_Center + sCurrentY );             //1，研究对象 
			NT35510_SetPointPixel ( usX_Center - sCurrentX, usY_Center + sCurrentY );             //2       
			NT35510_SetPointPixel ( usX_Center + sCurrentX, usY_Center - sCurrentY );             //3       
			NT35510_SetPointPixel ( usX_Center - sCurrentX, usY_Center - sCurrentY );             //4       
			NT35510_SetPointPixel ( usX_Center + sCurrentY, usY_Center + sCurrentX );             //5       
			NT35510_SetPointPixel ( usX_Center - sCurrentY, usY_Center + sCurrentX );             //6       
			NT35510_SetPointPixel ( usX_Center + sCurrentY, usY_Center - sCurrentX );             //7       
			NT35510_SetPointPixel ( usX_Center - sCurrentY, usY_Center - sCurrentX );             //8             
		}
		
		sCurrentX ++;  
		
		if ( sError < 0 ) 
			sError += 4 * sCurrentX + 6;	  
		
		else
		{
			sError += 10 + 4 * ( sCurrentX - sCurrentY );   
			sCurrentY --;
		} 	 
		
	}
	

}


/**
  * @brief  在NT35510显示器上显示一个英文字符
  * @param  usX :在特定扫描方向下字符的起始X坐标
  * @param  usY :在特定扫描方向下字符的起始Y坐标
  * @param  cChar :要显示的英文字符
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispChar_EN ( uint16_t usX, uint16_t usY, const char cChar )
{
	uint8_t  byteCount, bitCount,fontLength;	
	uint16_t ucRelativePositon;
	uint8_t *Pfont;
	
	//对ascii码表偏移（在字库中的ascii码表的起始位置为' ',因此减去32）
	ucRelativePositon = cChar - ' ';
	
	//每个字模的字节数
	fontLength = (LCD_Currentfonts->Width*LCD_Currentfonts->Height)/8;
		
	//字模首地址
	/*ascii码偏移值×每个字模的字节数，求出字模的偏移位置*/
	Pfont = (uint8_t *)&LCD_Currentfonts->table[ucRelativePositon * fontLength];
	
	//设置显示窗口
	NT35510_OpenWindow ( usX, usY, LCD_Currentfonts->Width, LCD_Currentfonts->Height);
	
	NT35510_Write_Cmd ( CMD_SetPixel );			

	//按字节读取字模数据
	//由于前面直接设置了显示窗口，显示数据会自动换行
	for ( byteCount = 0; byteCount < fontLength; byteCount++ )
	{
			//一位一位地处理要显示的颜色
			for ( bitCount = 0; bitCount < 8; bitCount++ )
			{
					if ( Pfont[byteCount] & (0x80>>bitCount) )
						NT35510_Write_Data ( CurrentTextColor );			
					else
						NT35510_Write_Data ( CurrentBackColor );
			}	
	}	
}


/**
  * @brief  在NT35510显示器上显示英文字符串
  * @param  line :在特定扫描方向下字符串的起始Y坐标
  *   @note  可以使用宏LINE(0)、LINE(1)等方式来指定行坐标，
  *          LINE(x)会根据当前选择的字体来计算Y坐标值。
  *          显示中文且使用LINE宏时，需要把英文字体设置成Font16x32
  * @param  pStr :要显示的英文字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispStringLine_EN (  uint16_t line, char * pStr )
{
	uint16_t usX = 0;
	
	while( * pStr != '\0' )
	{
		if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) > LCD_X_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			line += LCD_Currentfonts->Height;
		}
		
		if ( ( line - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) > LCD_Y_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			line = NT35510_DispWindow_Y_Star;
		}			
		
		NT35510_DispChar_EN ( usX, line, * pStr );
		
		usX +=  LCD_Currentfonts->Width;
	
	  pStr ++;
	}
	
}


/**
  * @brief  在NT35510显示器上显示英文字符串
  * @param  usX :在特定扫描方向下字符串的起始X坐标
  * @param  usY :在特定扫描方向下字符串的起始Y坐标
  * @param  pStr :要显示的英文字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispString_EN ( 	uint16_t usX, uint16_t usY, char * pStr )
{
	while( * pStr != '\0' )
	{
		if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) > LCD_X_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			usY += LCD_Currentfonts->Height;
		}
		
		if ( ( usY - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) > LCD_Y_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			usY = NT35510_DispWindow_Y_Star;
		}			
		
		NT35510_DispChar_EN ( usX, usY, * pStr );
		
		pStr ++;
		
		usX += LCD_Currentfonts->Width;
		
	}
	
}


/**
  * @brief  在NT35510显示器上显示中文字符
  * @param  usX :在特定扫描方向下字符的起始X坐标
  * @param  usY :在特定扫描方向下字符的起始Y坐标
  * @param  usChar :要显示的中文字符（国标码）
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispChar_CH ( uint16_t usX, uint16_t usY, uint16_t usChar )
{
	uint8_t rowCount, bitCount;
  uint32_t usTemp; 
	
//	占用空间太大，改成全局变量 
//	uint8_t ucBuffer [ WIDTH_CH_CHAR*HEIGHT_CH_CHAR/8 ];	
	
	//设置显示窗口
	NT35510_OpenWindow ( usX, usY, WIDTH_CH_CHAR, HEIGHT_CH_CHAR );
	
	NT35510_Write_Cmd ( CMD_SetPixel );
	
	//取出字模数据  
  GetGBKCode ( ucBuffer, usChar );	
	
	for ( rowCount = 0; rowCount < HEIGHT_CH_CHAR; rowCount++ )
	{
    /* 取出四个字节的数据，在lcd上即是一个汉字的一行 */
		usTemp = ucBuffer [ rowCount * 4 ];
		usTemp = ( usTemp << 8 );
		usTemp |= ucBuffer [ rowCount * 4 + 1 ];
		usTemp = ( usTemp << 8 );
		usTemp |= ucBuffer [ rowCount * 4 + 2 ];
		usTemp = ( usTemp << 8 );
		usTemp |= ucBuffer [ rowCount * 4 + 3 ];
		
		for ( bitCount = 0; bitCount < WIDTH_CH_CHAR; bitCount ++ )
		{			
			if ( usTemp & ( 0x80000000 >> bitCount ) )  //高位在前 
			  NT35510_Write_Data ( CurrentTextColor );				
			else
				NT35510_Write_Data ( CurrentBackColor );			
		}		
	}
	
}


/**
  * @brief  在NT35510显示器上显示中英文字符串
  * @param  usX :在特定扫描方向下字符串的起始X坐标
  * @param  usY :在特定扫描方向下字符串的起始Y坐标
  * @param  pStr :要显示的字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispString_EN_CH ( uint16_t usX, uint16_t usY, char * pStr )
{
	uint16_t usCh;
	
	while( * pStr != '\0' )
	{
		if ( * pStr <= 126 )	           	//英文字符
		{
			if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY += LCD_Currentfonts->Height;
			}
			
			if ( ( usY - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) > LCD_Y_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY = NT35510_DispWindow_Y_Star;
			}			
		
		  NT35510_DispChar_EN ( usX, usY, * pStr );
			
			usX +=  LCD_Currentfonts->Width;
		
		  pStr ++;

		}
		
		else	                            //中文字符
		{
			if ( ( usX - NT35510_DispWindow_X_Star + WIDTH_CH_CHAR ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY += HEIGHT_CH_CHAR;
			}
			
			if ( ( usY - NT35510_DispWindow_Y_Star + HEIGHT_CH_CHAR ) > LCD_Y_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY = NT35510_DispWindow_Y_Star;
			}	
			
			usCh = * ( uint16_t * ) pStr;	
			
			usCh = ( usCh << 8 ) + ( usCh >> 8 );		

			NT35510_DispChar_CH ( usX, usY, usCh );
			
			usX += WIDTH_CH_CHAR;
			
			pStr += 2;           //一个汉字两个字节 
		
    }
		
  }	
	
} 


/**
  * @brief  在NT35510显示器上显示中英文字符串（沿Y轴方向）
  * @param  usX :在特定扫描方向下字符串的起始X坐标
  * @param  usY :在特定扫描方向下字符串的起始Y坐标
  * @param  pStr :要显示的中英文字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispString_EN_CH_YDir (  uint16_t usX,uint16_t usY , char * pStr )
{
	uint16_t usCh;
	
	while( * pStr != '\0' )
	{
		if ( * pStr <= 126 )	           	//英文字符
		{
			if ( ( usY - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) > LCD_Y_LENGTH )
			{
				usY = NT35510_DispWindow_Y_Star;
				usX += LCD_Currentfonts->Width;
			}
			
			if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY = NT35510_DispWindow_Y_Star;
			}			
		
		  NT35510_DispChar_EN ( usX, usY, * pStr );
			
			pStr ++;
		
			usY +=  LCD_Currentfonts->Height;
		
		}
		
		else	                            //中文字符
		{
			if ( ( usY - NT35510_DispWindow_Y_Star + HEIGHT_CH_CHAR ) > LCD_Y_LENGTH )
			{
				usY = NT35510_DispWindow_Y_Star;
				usX += WIDTH_CH_CHAR;
			}
			
			if ( ( usX - NT35510_DispWindow_X_Star + WIDTH_CH_CHAR ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				usY = NT35510_DispWindow_Y_Star;
			}	
			
			usCh = * ( uint16_t * ) pStr;	
			
			usCh = ( usCh << 8 ) + ( usCh >> 8 );		

			NT35510_DispChar_CH ( usX, usY, usCh );
			
			pStr += 2;           //一个汉字两个字节 
			
			usY += HEIGHT_CH_CHAR;
		
    }
		
  }	
	
}


/**
  * @brief  NT35510背光LED控制
  * @param  enumState ：决定是否使能背光LED
  *   该参数为以下值之一：
  *     @arg ENABLE :使能背光LED
  *     @arg DISABLE :禁用背光LED
  * @retval 无
  */
void NT35510_BackLed_Control ( FunctionalState enumState )
{
	if ( enumState )
		 HAL_GPIO_WritePin ( NT35510_BK_PORT, NT35510_BK_PIN, GPIO_PIN_SET );	
	else
		 HAL_GPIO_WritePin ( NT35510_BK_PORT, NT35510_BK_PIN, GPIO_PIN_RESET );
		
}


/**
  * @brief  在NT35510显示器上显示英文字符串(沿Y轴方向)
  * @param  usX :在特定扫描方向下字符的起始X坐标
  * @param  usY :在特定扫描方向下字符的起始Y坐标
  * @param  pStr :要显示的英文字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispString_EN_YDir (	 uint16_t usX,uint16_t usY ,  char * pStr )
{	
	while ( * pStr != '\0' )
	{
		if ( ( usY - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) >LCD_Y_LENGTH  )
		{
			usY = NT35510_DispWindow_Y_Star;
			usX += LCD_Currentfonts->Width;
		}
		
		if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) >  LCD_X_LENGTH)
		{
			usX = NT35510_DispWindow_X_Star;
			usY = NT35510_DispWindow_Y_Star;
		}
		
		NT35510_DispChar_EN ( usX, usY, * pStr);
		
		pStr ++;
		
		usY += LCD_Currentfonts->Height;		
	}	
}


/**
  * @brief  在NT35510显示器上显示中文字符串
  * @param  usX :在特定扫描方向下字符串的起始X坐标
  * @param  usY :在特定扫描方向下字符串的起始Y坐标
  * @param  pStr :要显示的中文字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispString_CH ( 	uint16_t usX , uint16_t usY, char * pStr )
{	
	uint16_t usCh;

	
	while( * pStr != '\0' )
	{		
		if ( ( usX - NT35510_DispWindow_X_Star + WIDTH_CH_CHAR ) > LCD_X_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			usY += HEIGHT_CH_CHAR;
		}
		
		if ( ( usY - NT35510_DispWindow_Y_Star + HEIGHT_CH_CHAR ) > LCD_Y_LENGTH )
		{
			usX = NT35510_DispWindow_X_Star;
			usY = NT35510_DispWindow_Y_Star;
		}	
		
		usCh = * ( uint16_t * ) pStr;	
	  usCh = ( usCh << 8 ) + ( usCh >> 8 );

		NT35510_DispChar_CH ( usX, usY, usCh );
		
		usX += WIDTH_CH_CHAR;
		
		pStr += 2;           //一个汉字两个字节 

	}	   
	
}


/**
  * @brief  在NT35510显示器上显示中英文字符串
  * @param  line ：在特定扫描方向下字符串的起始Y坐标
  *   本参数可使用宏LINE(0)、LINE(1)等方式指定文字坐标，
  *   宏LINE(x)会根据当前选择的字体来计算Y坐标值。
	*		显示中文且使用LINE宏时，需要把英文字体设置成Font16x32
  * @param  pStr ：要显示的字符串的首地址
  * @note  可使用LCD_SetBackColor、LCD_SetTextColor、LCD_SetColors函数设置颜色
  * @retval 无
  */
void NT35510_DispStringLine_EN_CH (  uint16_t line, char * pStr )
{
	uint16_t usCh;
	uint16_t usX = 0;
	
	while( * pStr != '\0' )
	{
		if ( * pStr <= 126 )	           	//英文字符
		{
			if ( ( usX - NT35510_DispWindow_X_Star + LCD_Currentfonts->Width ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				line += LCD_Currentfonts->Height;
			}
			
			if ( ( line - NT35510_DispWindow_Y_Star + LCD_Currentfonts->Height ) > LCD_Y_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				line = NT35510_DispWindow_Y_Star;
			}			
		
		  NT35510_DispChar_EN ( usX, line, * pStr );
			
			usX +=  LCD_Currentfonts->Width;
		
		  pStr ++;

		}
		
		else	                            //汉字字符
		{
			if ( ( usX - NT35510_DispWindow_X_Star + WIDTH_CH_CHAR ) > LCD_X_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				line += HEIGHT_CH_CHAR;
			}
			
			if ( ( line - NT35510_DispWindow_Y_Star + HEIGHT_CH_CHAR ) > LCD_Y_LENGTH )
			{
				usX = NT35510_DispWindow_X_Star;
				line = NT35510_DispWindow_Y_Star;
			}	
			
			usCh = * ( uint16_t * ) pStr;	
			
			usCh = ( usCh << 8 ) + ( usCh >> 8 );		

			NT35510_DispChar_CH ( usX, line, usCh );
			
			usX += WIDTH_CH_CHAR;
			
			pStr += 2;           //一个汉字两个字节 
		
    }
		
  }	
}


/***********************缩放字体****************************/
#define ZOOMMAXBUFF 16384
uint8_t zoomBuff[ZOOMMAXBUFF] = {0};	//用于缩放的缓存，最大支持到128*128
uint8_t zoomTempBuff[1024] = {0};

/**
  * @brief  缩放字模，缩放后的字模由1个像素点由8个数据位来表示
										0x01表示笔迹，0x00表示空白区
  * @param  in_width ：原始字符宽度
  * @param  in_heig ：原始字符高度
  * @param  out_width ：缩放后的字符宽度
  * @param  out_heig ：缩放后的字符高度
  * @param  in_ptr ：字库输入指针	注意：1pixel 1bit
  * @param  out_ptr ：缩放后的字符输出指针 注意: 1pixel 8bit
  *		out_ptr实际上没有正常输出，改成了直接输出到全局指针zoomBuff中
  * @param  en_cn ：0为英文，1为中文
  * @retval 无
  */
void NT35510_zoomChar(uint16_t in_width,	//原始字符宽度
									uint16_t in_heig,		//原始字符高度
									uint16_t out_width,	//缩放后的字符宽度
									uint16_t out_heig,	//缩放后的字符高度
									uint8_t *in_ptr,	//字库输入指针	注意：1pixel 1bit
									uint8_t *out_ptr, //缩放后的字符输出指针 注意: 1pixel 8bit
									uint8_t en_cn)		//0为英文，1为中文	
{
	uint8_t *pts,*ots;
	//根据源字模及目标字模大小，设定运算比例因子，左移16是为了把浮点运算转成定点运算
	unsigned int xrIntFloat_16=(in_width<<16)/out_width+1; 
  unsigned int yrIntFloat_16=(in_heig<<16)/out_heig+1;
	
	unsigned int srcy_16=0;
	unsigned int y,x;
	uint8_t *pSrcLine;
	
	uint16_t byteCount,bitCount;
	
	//检查参数是否合法
	if(in_width > 48) return;												//字库不允许超过48像素
	if(in_width * in_heig == 0) return;	
	if(in_width * in_heig > 48*48 ) return; 					//限制输入最大 48*48
	
	if(out_width * out_heig == 0) return;	
	if(out_width * out_heig >= ZOOMMAXBUFF ) return; //限制最大缩放 128*128
	pts = (uint8_t*)&zoomTempBuff;
	
	//为方便运算，字库的数据由1 pixel/1bit 映射到1pixel/8bit
	//0x01表示笔迹，0x00表示空白区
	if(en_cn == 0x00)//英文
	{
		//英文和中文字库上下边界不对，可在此处调整。需要注意tempBuff防止溢出
			for(byteCount=0;byteCount<in_heig*in_width/8;byteCount++)	
			{
				for(bitCount=0;bitCount<8;bitCount++)
					{						
						//把源字模数据由位映射到字节
						//in_ptr里bitX为1，则pts里整个字节值为1
						//in_ptr里bitX为0，则pts里整个字节值为0
						*pts++ = (in_ptr[byteCount] & (0x80>>bitCount))?1:0; 
					}
			}				
	}
	else //中文
	{			
			for(byteCount=0;byteCount<in_heig*in_width/8;byteCount++)	
			{
				for(bitCount=0;bitCount<8;bitCount++)
					{						
						//把源字模数据由位映射到字节
						//in_ptr里bitX为1，则pts里整个字节值为1
						//in_ptr里bitX为0，则pts里整个字节值为0
						*pts++ = (in_ptr[byteCount] & (0x80>>bitCount))?1:0; 
					}
			}		
	}

	//zoom过程
	pts = (uint8_t*)&zoomTempBuff;	//映射后的源数据指针
	ots = (uint8_t*)&zoomBuff;	//输出数据的指针
	for (y=0;y<out_heig;y++)	/*行遍历*/
    {
				unsigned int srcx_16=0;
        pSrcLine=pts+in_width*(srcy_16>>16);				
        for (x=0;x<out_width;x++) /*行内像素遍历*/
        {
            ots[x]=pSrcLine[srcx_16>>16]; //把源字模数据复制到目标指针中
            srcx_16+=xrIntFloat_16;			//按比例偏移源像素点
        }
        srcy_16+=yrIntFloat_16;				  //按比例偏移源像素点
        ots+=out_width;						
    }
	/*！！！缩放后的字模数据直接存储到全局指针zoomBuff里了*/
	out_ptr = (uint8_t*)&zoomBuff;	//out_ptr没有正确传出，后面调用直接改成了全局变量指针！
	
	/*实际中如果使用out_ptr不需要下面这一句！！！
		只是因为out_ptr没有使用，会导致warning。强迫症*/
	out_ptr++; 
}			


/**
  * @brief  利用缩放后的字模显示字符
  * @param  usX ：字符显示位置x
  * @param  usY ：字符显示位置y
  * @param  Font_width ：字符宽度
  * @param  Font_Heig：字符高度
  * @param  c ：要显示的字模数据
  * @param  DrawModel ：是否反色显示 
  * @retval 无
  */
void NT35510_DrawChar_Ex(uint16_t usX, //字符显示位置x
												uint16_t usY, //字符显示位置y
												uint16_t Font_width, //字符宽度
												uint16_t Font_Height,  //字符高度 
												uint8_t *c,						//字模数据
												uint16_t DrawModel)		//是否反色显示
{
  uint32_t index = 0, counter = 0;

	//设置显示窗口
	NT35510_OpenWindow ( usX, usY, Font_width, Font_Height);
	
	NT35510_Write_Cmd ( CMD_SetPixel );		
	
	//按字节读取字模数据
	//由于前面直接设置了显示窗口，显示数据会自动换行
	for ( index = 0; index < Font_Height; index++ )
	{
			//一位一位处理要显示的颜色
			for ( counter = 0; counter < Font_width; counter++ )
			{
					//缩放后的字模数据，以一个字节表示一个像素位
					//整个字节值为1表示该像素为笔迹
					//整个字节值为0表示该像素为背景
					if ( *c++ == DrawModel )
						NT35510_Write_Data ( CurrentBackColor );			
					else
						NT35510_Write_Data ( CurrentTextColor );
			}	
	}	
}


/**
  * @brief  利用缩放后的字模显示字符串
  * @param  x ：字符显示位置x
  * @param  y ：字符显示位置y
  * @param  Font_width ：字符宽度，英文字符在此基础上/2。注意为偶数
  * @param  Font_Heig：字符高度，注意为偶数
  * @param  ptr ：要显示的字符串
  * @param  DrawModel ：是否反色显示 
  * @retval 无
  */
void NT35510_DisplayStringEx(uint16_t x, 		//字符显示位置x
														 uint16_t y, 				//字符显示位置y
														 uint16_t Font_width,	//要显示的字体宽度，英文字符在此基础上/2。注意为偶数
														 uint16_t Font_Height,	//要显示的字体高度，注意为偶数
														 uint8_t *ptr,					//显示的字符内容
														 uint16_t DrawModel)  //是否反色显示
{
	uint16_t Charwidth = Font_width; //默认为Font_width，英文宽度为中文宽度的一半
	uint8_t *psr;
	uint8_t Ascii;	//英文
	uint16_t usCh;  //中文
	
	//占用空间太大，改成全局变量	
	//	uint8_t ucBuffer [ WIDTH_CH_CHAR*HEIGHT_CH_CHAR/8 ];	
	
	while ( *ptr != '\0')
	{
			/****处理换行*****/
			if ( ( x - NT35510_DispWindow_X_Star + Charwidth ) > LCD_X_LENGTH )
			{
				x = NT35510_DispWindow_X_Star;
				y += Font_Height;
			}
			
			if ( ( y - NT35510_DispWindow_Y_Star + Font_Height ) > LCD_Y_LENGTH )
			{
				x = NT35510_DispWindow_X_Star;
				y = NT35510_DispWindow_Y_Star;
			}	
			
		if(*ptr > 0x80) //如果是中文
		{			
			Charwidth = Font_width;
			usCh = * ( uint16_t * ) ptr;				
			usCh = ( usCh << 8 ) + ( usCh >> 8 );
			GetGBKCode ( ucBuffer, usCh );	//取字模数据
			//缩放字模数据，源字模为32*32
			NT35510_zoomChar(WIDTH_CH_CHAR,HEIGHT_CH_CHAR,Charwidth,Font_Height,(uint8_t *)&ucBuffer,psr,1); 
			//显示单个字符
			NT35510_DrawChar_Ex(x,y,Charwidth,Font_Height,(uint8_t*)&zoomBuff,DrawModel);
			x+=Charwidth;
			ptr+=2;
		}
		else
		{
				Charwidth = Font_width / 2;
				Ascii = *ptr - 32;
				//使用16*32字体缩放字模数据
				NT35510_zoomChar(16,32,Charwidth,Font_Height,(uint8_t *)&Font16x32.table[Ascii * Font16x32.Height*Font16x32.Width/8],psr,0);
			  //显示单个字符
				NT35510_DrawChar_Ex(x,y,Charwidth,Font_Height,(uint8_t*)&zoomBuff,DrawModel);
				x+=Charwidth;
				ptr++;
		}
	}
}


/**
  * @brief  利用缩放后的字模显示字符串(沿Y轴方向)
  * @param  x ：字符显示位置x
  * @param  y ：字符显示位置y
  * @param  Font_width ：字符宽度，英文字符在此基础上/2。注意为偶数
  * @param  Font_Heig：字符高度，注意为偶数
  * @param  ptr ：要显示的字符串
  * @param  DrawModel ：是否反色显示 
  * @retval 无
  */
void NT35510_DisplayStringEx_YDir(uint16_t x, 		//字符显示位置x
																		 uint16_t y, 				//字符显示位置y
																		 uint16_t Font_width,	//要显示的字体宽度，英文字符在此基础上/2。注意为偶数
																		 uint16_t Font_Height,	//要显示的字体高度，注意为偶数
																		 uint8_t *ptr,					//显示的字符内容
																		 uint16_t DrawModel)  //是否反色显示
{
	uint16_t Charwidth = Font_width; //默认为Font_width，英文宽度为中文宽度的一半
	uint8_t *psr;
	uint8_t Ascii;	//英文
	uint16_t usCh;  //中文
	uint8_t ucBuffer [ WIDTH_CH_CHAR*HEIGHT_CH_CHAR/8 ];	
	
	while ( *ptr != '\0')
	{			
			//统一使用汉字的宽高来计算换行
			if ( ( y - NT35510_DispWindow_X_Star + Font_width ) > LCD_X_LENGTH )
			{
				y = NT35510_DispWindow_X_Star;
				x += Font_width;
			}
			
			if ( ( x - NT35510_DispWindow_Y_Star + Font_Height ) > LCD_Y_LENGTH )
			{
				y = NT35510_DispWindow_X_Star;
				x = NT35510_DispWindow_Y_Star;
			}	
			
		if(*ptr > 0x80) //如果是中文
		{			
			Charwidth = Font_width;
			usCh = * ( uint16_t * ) ptr;				
			usCh = ( usCh << 8 ) + ( usCh >> 8 );
			GetGBKCode ( ucBuffer, usCh );	//取字模数据
			//缩放字模数据，源字模为32*32
			NT35510_zoomChar(WIDTH_CH_CHAR,HEIGHT_CH_CHAR,Charwidth,Font_Height,(uint8_t *)&ucBuffer,psr,1); 
			//显示单个字符
			NT35510_DrawChar_Ex(x,y,Charwidth,Font_Height,(uint8_t*)&zoomBuff,DrawModel);
			y+=Font_Height;
			ptr+=2;
		}
		else
		{
				Charwidth = Font_width / 2;
				Ascii = *ptr - 32;
				//使用16*32字体缩放字模数据
				NT35510_zoomChar(16,32,Charwidth,Font_Height,(uint8_t *)&Font16x32.table[Ascii * Font16x32.Height*Font16x32.Width/8],psr,0);
			  //显示单个字符
				NT35510_DrawChar_Ex(x,y,Charwidth,Font_Height,(uint8_t*)&zoomBuff,DrawModel);
				y+=Font_Height;
				ptr++;
		}
	}
}


/**
  * @brief  设置英文字体类型
  * @param  fonts: 指定要选择的字体
	*		参数为以下值之一
  * 	@arg：Font24x48;
  * 	@arg：Font16x32;
  * 	@arg：Font8x16;
  * @retval 无
  */
void LCD_SetFont(sFONT *fonts)
{
  LCD_Currentfonts = fonts;
}


/**
  * @brief  获取当前字体类型
  * @param  无
  * @retval 返回当前字体类型
  */
sFONT *LCD_GetFont(void)
{
  return LCD_Currentfonts;
}


/**
  * @brief  设置LCD的前景(字体)及背景颜色,RGB565
  * @param  TextColor: 指定前景(字体)颜色
  * @param  BackColor: 指定背景颜色
  * @retval 无
  */
void LCD_SetColors(uint16_t TextColor, uint16_t BackColor) 
{
  CurrentTextColor = TextColor; 
  CurrentBackColor = BackColor;
}


/**
  * @brief  获取LCD的前景(字体)及背景颜色,RGB565
  * @param  TextColor: 用来存储前景(字体)颜色的指针变量
  * @param  BackColor: 用来存储背景颜色的指针变量
  * @retval 无
  */
void LCD_GetColors(uint16_t *TextColor, uint16_t *BackColor)
{
  *TextColor = CurrentTextColor;
  *BackColor = CurrentBackColor;
}


/**
  * @brief  设置LCD的前景(字体)颜色,RGB565
  * @param  Color: 指定前景(字体)颜色 
  * @retval 无
  */
void LCD_SetTextColor(uint16_t Color)
{
  CurrentTextColor = Color;
}


/**
  * @brief  设置LCD的背景颜色,RGB565
  * @param  Color: 指定背景颜色 
  * @retval 无
  */
void LCD_SetBackColor(uint16_t Color)
{
  CurrentBackColor = Color;
}


/**
  * @brief  清除某行文字
  * @param  Line: 指定要删除的行
  *   本参数可使用宏LINE(0)、LINE(1)等方式指定要删除的行，
  *   宏LINE(x)会根据当前选择的字体来计算Y坐标值，并删除当前字体高度的第x行。
  * @retval 无
  */
void NT35510_ClearLine(uint16_t Line)
{
  NT35510_Clear(0,Line,LCD_X_LENGTH,((sFONT *)LCD_GetFont())->Height);	/* 清屏，显示全黑 */

}


/******END OF FILE****/
