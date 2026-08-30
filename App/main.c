#include <stdio.h>
#include "main.h"
#include "stm32f4xx.h"
#include "drv_led.h"
#include "drv_systick.h"
#include "os_include.h"
#include "app_config.h"

int main(void)
{
    INT32U led_tick;

    /* APP链接在0x08020000，重定位中断向量表 */
    SCB->VTOR = 0x08020000;

    HAL_Init();
    app_config_init();

    led_tick = SYSTICK_GetMsTick();
    printf("main start\r\n");
    // 未检测到有效用户，LED闪烁提示
    while (1)
    {
        /* 内核调度：软件定时器倒计时+错误管理 */
        OS_TmrTskEntry();
        OS_ErrTskEntry();
        if (SYSTICK_GetMsTick() - led_tick >= 500) {
            led_tick = SYSTICK_GetMsTick();
            LED_PURPLE;
        }
    }
}
