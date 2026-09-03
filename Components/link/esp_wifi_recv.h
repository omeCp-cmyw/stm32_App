#ifndef __ESP_WIFI_RECV_H
#define __ESP_WIFI_RECV_H

#include "stm32f4xx.h"
#include <stdint.h>

/*
 * +IPD负载回调：按链路注册，收到该链路数据时调用
 * link: 链路号(0~4)，data/len: 负载
 */
typedef void (*WIFI_RECV_IPD_CB)(uint8_t link, const uint8_t *data, uint16_t len);

/*
 * 链路关闭事件回调：收到"<link>,CLOSED"行时调用
 * 注意：主动CIPCLOSE的应答也会触发，上层需自行区分
 */
typedef void (*WIFI_RECV_CLOSE_CB)(uint8_t link);

/*******************************************************************
** 函数名	: WIFI_RecvInit
** 函数描述	: 接收模块初始化：启动接收扫描定时器
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_RecvInit(void);

/*******************************************************************
** 函数名	: WIFI_RecvSetIpdCb
** 函数描述	: 注册指定链路的+IPD负载处理回调，收到该链路数据时调用
** 参数		: [in] link: 链路号(0~4)
**          : [in] cb: 负载回调，NULL取消注册
** 返回		: 无
********************************************************************/
void WIFI_RecvSetIpdCb(uint8_t link, WIFI_RECV_IPD_CB cb);

/*******************************************************************
** 函数名	: WIFI_RecvSetCloseCb
** 函数描述	: 注册指定链路的关闭事件回调，收到"<link>,CLOSED"时调用
** 参数		: [in] link: 链路号(0~4)
**          : [in] cb: 关闭回调，NULL取消注册
** 返回		: 无
********************************************************************/
void WIFI_RecvSetCloseCb(uint8_t link, WIFI_RECV_CLOSE_CB cb);

#endif /* __ESP_WIFI_RECV_H */
