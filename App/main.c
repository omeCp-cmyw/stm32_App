#include <stdio.h>
#include "main.h"
#include "drv_systick.h"
#include "drv_iwdg.h"
#include "drv_uart_reg.h"
#include "osal.h"
#include "app_config.h"
#include "app_gateway.h"
#include "ymodem.h"
#include "drv_uart.h"


int main(void)
{
    uint32_t reset_csr;

    /* APP链接在0x08020000，重定位中断向量表 */
    SCB->VTOR = 0x08020000;

    HAL_Init();

    /* 读取复位原因标志：RCC_CSR在掉电/上电/引脚复位后被硬件清空，
     * 软件复位与看门狗复位可跨复位保留，读后软件清除 */
    reset_csr = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* 板级/内核/链路初始化 */
    app_config_init();

    /* 业务编排初始化：物模型绑定+传感器注册+云平台接入 */
    app_gateway_init();

    /* 打印复位原因：CSR全零为掉电/上电/引脚类硬件复位 */
    if (reset_csr == 0) {
        printf("reset cause: power/brown-out/pin (CSR cleared)\r\n");
    } else {
        if (reset_csr & RCC_CSR_IWDGRSTF) {
            printf("reset cause: IWDG\r\n");
        }
        if (reset_csr & RCC_CSR_WWDGRSTF) {
            printf("reset cause: WWDG\r\n");
        }
        if (reset_csr & RCC_CSR_SFTRSTF) {
            printf("reset cause: software\r\n");
        }
        if (reset_csr & RCC_CSR_PINRSTF) {
            printf("reset cause: NRST pin\r\n");
        }
        if (reset_csr & RCC_CSR_BORRSTF) {
            printf("reset cause: BOR\r\n");
        }
        if (reset_csr & RCC_CSR_PORRSTF) {
            printf("reset cause: power-on\r\n");
        }
        if (reset_csr & RCC_CSR_LPWRRSTF) {
            printf("reset cause: low-power\r\n");
        }
    }

    printf("main start\r\n");
    while (1)
    {
        /* 喂狗：主循环任一环节阻塞超过3s即复位 */
        IWDG_Feed();
        /* 内核调度：软件定时器调度+诊断轮询+喂狗 */
        osal_task_loop();
        /* 升级窗口开启期间业务让路：对齐备份工程升级时主循环仅内核调度，
           保证升级口数据泵实时性，防止业务拖慢主循环导致环缓冲溢出丢包 */
        if (!FW_UPG_YM_IsArmed()) {
            /* 业务调度：传感器采集轮询/状态指示/告警上报 */
            app_gateway_loop();
        }
    }
}
