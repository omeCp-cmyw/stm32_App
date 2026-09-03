#ifndef __DRV_BEEP_H
#define	__DRV_BEEP_H

#include "stm32f4xx.h"

/*******************************************************/

#define BEEP_PIN                  GPIO_PIN_7                
#define BEEP_GPIO_PORT            GPIOG                     
          
/************************************************************/


/* 定义控制IO的宏 */
#define BEEP_TOGGLE		HAL_GPIO_TogglePin(BEEP_GPIO_PORT,BEEP_PIN);
#define BEEP_OFF		HAL_GPIO_WritePin(BEEP_GPIO_PORT,BEEP_PIN,GPIO_PIN_RESET);
#define BEEP_ON			HAL_GPIO_WritePin(BEEP_GPIO_PORT,BEEP_PIN,GPIO_PIN_SET);
                        

void BEEP_GPIO_Config(void);

#endif /* __DRV_BEEP_H */
