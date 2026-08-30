#include "drv_beep.h"   

/*******************************************************************
** 函数名	: BEEP_GPIO_Config
** 函数描述	: 初始化控制蜂鸣器的GPIO引脚
** 参数		: 无
** 返回		: 无
********************************************************************/
void BEEP_GPIO_Config(void)
{		
    /*定义一个GPIO_InitTypeDef类型的结构体*/
    GPIO_InitTypeDef  GPIO_InitStruct;
    
    /*开启蜂鸣器相关的GPIO外设时钟*/
    __GPIOG_CLK_ENABLE();
  
    /*选择要控制的GPIO引脚*/															   
    GPIO_InitStruct.Pin = BEEP_PIN;	
    /*设置引脚的输出类型为推挽输出*/
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;  
    /*设置引脚为上拉模式*/
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    /*设置引脚速率为高速 */   
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH; 
    /*调用库函数，使用上面配置的GPIO_InitStructure初始化GPIO*/
    HAL_GPIO_Init(BEEP_GPIO_PORT, &GPIO_InitStruct);	
	
    BEEP_OFF;
}

/*********************************************END OF FILE**********************/
