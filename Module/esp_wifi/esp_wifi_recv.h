#ifndef __ESP_WIFI_RECV_H
#define __ESP_WIFI_RECV_H

#include "stm32f4xx.h"
#include <stdint.h>

/* +IPD二进制负载回调 */
typedef void (*WIFI_RECV_IPD_CB)(const uint8_t *data, uint16_t len);

/*******************************************************************
** 函数名	: WIFI_RecvInit
** 函数描述	: 接收模块初始化：启动接收扫描定时器
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_RecvInit(void);

/*******************************************************************
** 函数名	: WIFI_RecvSetIpdCb
** 函数描述	: 注册+IPD负载处理回调，收到链路0数据时调用
** 参数		: [in] cb: 负载回调
** 返回		: 无
********************************************************************/
void WIFI_RecvSetIpdCb(WIFI_RECV_IPD_CB cb);

#endif /* __ESP_WIFI_RECV_H */
