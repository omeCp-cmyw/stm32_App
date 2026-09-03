#ifndef __ESP_WIFI_MMI_H
#define __ESP_WIFI_MMI_H

#include "stm32f4xx.h"
#include <stdint.h>
#include "drv_uart_reg.h"

/* WiFi模块串口：USART2 */
#define WIFI_COM    DRV_UART_COM_1

/* WiFi与NTP配置：由App层gateway_config.h集中管理 */
#include "gateway_config.h"
#define WIFI_SSID       GW_WIFI_SSID
#define WIFI_PASS       GW_WIFI_PASS
#define WIFI_NTP_SERVER GW_NTP_SERVER
#define WIFI_NTP_PORT   GW_NTP_PORT

/* WiFi STA连接状态 */
typedef enum {
    WIFI_STA_INIT = 0,      /* 初始化中 */
    WIFI_STA_CONNECTING,    /* 连接路由器中 */
    WIFI_STA_CONNECTED,     /* 路由器已连接 */
    WIFI_STA_LOST,          /* 已连接后掉线(自动重连中) */
    WIFI_STA_FAIL           /* 连接失败(重试中) */
} WIFI_STA_E;

/*******************************************************************
** 函数名	: WIFI_MMI_Init
** 函数描述	: WiFi模块初始化：复位脚配置、发送/接收模块初始化，
**			注册IPD回调并启动状态机，须在串口与定时器初始化后调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_MMI_Init(void);

/*******************************************************************
** 函数名	: WIFI_MMI_IsReady
** 函数描述	: 查询初始化流程是否完成(基础指令+NTP校时收尾)
** 参数		: 无
** 返回		: 1完成，0未完成
********************************************************************/
uint8_t WIFI_MMI_IsReady(void);

/*******************************************************************
** 函数名	: WIFI_MMI_GetSta
** 函数描述	: 查询STA连接状态
** 参数		: 无
** 返回		: WIFI_STA_E状态
********************************************************************/
WIFI_STA_E WIFI_MMI_GetSta(void);

/*******************************************************************
** 函数名	: WIFI_MMI_OnStaEvent
** 函数描述	: STA连接事件上报(接收侧"WIFI CONNECTED/DISCONNECT"行触发)：
**			记录状态，已连接后掉线自动重启重连/复位流程
** 参数		: [in] connected: 1已连接, 0断开
** 返回		: 无
********************************************************************/
void WIFI_MMI_OnStaEvent(uint8_t connected);

/*******************************************************************
** 函数名	: WIFI_MMI_Reset
** 函数描述	: 外部触发模块整体复位初始化(open连续失败自愈等场景)：
**			拉复位脚+重跑AT指令序列恢复CIPMUX/路由器连接
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_MMI_Reset(void);

#endif /* __ESP_WIFI_MMI_H */
