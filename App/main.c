#include <stdio.h>
#include "main.h"
#include "drv_led.h"
#include "drv_systick.h"
#include "drv_iwdg.h"
#include "drv_uart_reg.h"
#include "os_include.h"
#include "app_config.h"
#include "drv_uart.h"


int main(void)
{
    INT32U led_tick;
    uint8_t iwdg_reset;

    /* APP链接在0x08020000，重定位中断向量表 */
    SCB->VTOR = 0x08020000;

    HAL_Init();

    /* 读取复位标志：看门狗复位后由硬件置位，须软件清除 */
    iwdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET);
    __HAL_RCC_CLEAR_RESET_FLAGS();

    app_config_init();

    if (iwdg_reset) {
        printf("reset by IWDG\r\n");
    }

    led_tick = SYSTICK_GetMsTick();
    printf("main start\r\n");
    // 未检测到有效用户，LED闪烁提示
    while (1)
    {
        /* 喂狗：主循环任一环节阻塞超过3s即复位 */
        IWDG_Feed();
        /* 内核调度：软件定时器倒计时+错误管理 */
        OS_TmrTskEntry();
        OS_ErrTskEntry();
        if (SYSTICK_GetMsTick() - led_tick >= 500) {
            led_tick = SYSTICK_GetMsTick();
            LED_PURPLE;
        }
    }
}
