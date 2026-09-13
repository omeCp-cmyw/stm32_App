/********************************************************************************
**
** 文件名:     main.h
** 版权所有:   无
** 文件描述:   该模块主要实现系统初始化相关宏定义
**
*********************************************************************************/


#ifndef __MAIN_H
#define __MAIN_H

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "../Tools/debug.h"
#include "../Platform/drv_uart/bsp_debug_usart.h"
#include "../Platform/drv_led/bsp_led.h"
#include "../Platform/drv_eth/bsp_eth.h"

/* FreeRTOS头文件 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

void BSP_Init(void);

#endif /* __MAIN_H */
