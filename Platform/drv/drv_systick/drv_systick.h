#ifndef __DRV_SYSTICK_H
#define __DRV_SYSTICK_H

#include "stm32f4xx.h"

void SYSTICK_Init(void);
void SYSTICK_DelayUs(uint32_t nTime);

uint32_t SYSTICK_GetTick(void);
uint32_t SYSTICK_GetMsTick(void);
void     SYSTICK_DelayMs(uint32_t period);
uint8_t  SYSTICK_InstallTickFun(void (*fun)(void));
void     SYSTICK_Handler(void);

#endif /* __DRV_SYSTICK_H */
