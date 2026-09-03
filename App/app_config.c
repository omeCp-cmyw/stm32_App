#include "stm32f4xx.h"
#include <stdio.h>
#include "osal.h"
#include "board.h"
#include "link.h"
#include "fw_upgrade.h"
#include "ymodem.h"
#include "drv_iwdg.h"
#include "app_config.h"


/*******************************************************************
** 函数名	: app_config_init
** 函数描述	: 应用初始化总入口：板级初始化+OSAL+链路层
**          : (云平台接入等业务组件由app_gateway编排初始化)
** 参数		: 无
** 返回		: 无
********************************************************************/
void app_config_init(void)
{
    printf("app_config_init start\r\n");

    /* 板级初始化：HAL/时钟/外设驱动/看门狗 */
    board_init();

    /* 内核初始化：OSAL软件定时器+错误管理，须在SYSTICK_Init后 */
    osal_init();

    /* 链路层：ESP8266 AT引擎+TCP链路抽象，内部用软件定时器，须在osal_init后 */
    link_init();

    /* 固件升级：主控+串口Ymodem通道，Init内开升级窗口主动发'C'等发送方 */
    FW_UPG_Init();
    FW_UPG_YM_Init();

    /* 独立看门狗：3s超时(升级擦写Flash最长阻塞约1s，留足余量)，主循环喂狗。
       对齐备份工程时序：所有初始化完成后最后启动，启动路径不受看门狗约束 */
    IWDG_Init(3000);
    osal_register_watchdog(IWDG_Feed);

    printf("app_config_init end\r\n");
}
