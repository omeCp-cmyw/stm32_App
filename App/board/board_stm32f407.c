#include <stdio.h>
#include "stm32f4xx.h"
#include "board.h"
#include "osal.h"
#include "drv_led.h"
#include "drv_dma.h"
#include "drv_tim7.h"
#include "drv_systick.h"
#include "drv_uart.h"
#include "drv_iwdg.h"
#include "drv_beep.h"

/*
 * STM32F407板级实现：资源映射集中在此文件。
 * 换板子（如换WiFi串口/换传感器I2C）只改这里，应用层与组件层无感。
 */

static void SystemClock_Config(void);

/*******************************************************************
** 函数名	: board_init
** 函数描述	: 板级初始化：HAL/时钟/SysTick/外设驱动/看门狗，
**			须先于所有组件初始化调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void board_init(void)
{
    printf("board_init start\r\n");

    /* HAL 库初始化（必须在所有 HAL API 调用之前执行） */
    HAL_Init();

    /* 配置系统时钟为 168 MHz */
    SystemClock_Config();

    /* SysTick配置为10us中断，提供SYSTICK_DelayUs/SYSTICK_DelayMs延时 */
    SYSTICK_Init();

    /* LED 引脚初始化 */
    LED_GPIO_Config();

    /* 蜂鸣器引脚初始化 */
    BEEP_GPIO_Config();

    /* 内存到内存 DMA 初始化 */
    MTM_DMA_Init();

    /* 串口驱动初始化：含USART1硬件配置+打开DRV_UART_COM_0，保留DMA+空闲接收 */
    DRV_UART_InitDrv();

    /* TIM7 调试定时器：10s周期，中断置标志，主循环中printf打印 */
    TIM7_Debug_Config(10000);

    printf("board_init end\r\n");
}

/*******************************************************************
** 函数名	: SystemClock_Config
** 函数描述	: 系统时钟配置，HSE + PLL，SYSCLK = 168MHz
** 参数		: 无
** 返回		: 无
********************************************************************/
static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;

  /* 使能电源控制时钟 */
  __HAL_RCC_PWR_CLK_ENABLE();

  /* 电压调节模式配置：当系统频率低于最大值时，
     可通过调节电压等级优化功耗，详见数据手册。 */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* 使能 HSE 振荡器，并激活 PLL（以 HSE 作为时钟源） */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    while(1) {};
  }

  /* 选择 PLL 作为系统时钟源，并配置 HCLK、PCLK1、PCLK2 分频系数 */
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    while(1) {};
  }

  /* STM32F405x/407x/415x/417x Revision Z 设备支持预取功能 */
  if (HAL_GetREVID() == 0x1001)
  {
    /* 使能 Flash 预取缓冲 */
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
  }
}
