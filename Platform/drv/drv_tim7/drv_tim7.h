#ifndef __DRV_TIM7_H
#define	__DRV_TIM7_H

#include "stm32f4xx.h"
#include "stm32f4xx_hal_tim.h"
#include <stdint.h>


#define BASIC_TIM                   TIM7
#define BASIC_TIM_IRQn              TIM7_IRQn
#define PERIOD_TIMER_IRQHandler     TIM7_IRQHandler

void TIM7_Debug_Config(uint32_t period_ms); // TIM7调试配置

#endif /* __DRV_TIM7_H */
