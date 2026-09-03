#ifndef __DRV_IWDG_H
#define	__DRV_IWDG_H

#include "stm32f4xx.h"
#include "stm32f4xx_hal_iwdg.h"
#include <stdint.h>

void IWDG_Init(uint32_t timeout_ms); // IWDG初始化，毫秒级超时
void IWDG_Feed(void);                // 喂狗

#endif /* __DRV_IWDG_H */
