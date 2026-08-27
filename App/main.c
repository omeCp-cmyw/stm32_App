#include <stdio.h>
#include "main.h"
#include "stm32f4xx.h"
#include "drv_led.h"
#include "drv_uart1.h"
#include "app_config.h"

int main(void)
{
    /* APP链接在0x08020000（Bootloader之后），必须手动重定位中断向量表，
       否则定时器中断进来时取到错误向量会直接跑飞复位 */
    SCB->VTOR = 0x08020000;

    HAL_Init();
    app_config_init();

    printf("1234 APP\r\n");


    // 未检测到有效用户，LED闪烁提示
    while (1)
    {
        LED1_TOGGLE;
        HAL_Delay(500);
        LED_BLUE;
        HAL_Delay(500);
    }
}
