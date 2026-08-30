#ifndef __ESP_WIFI_SEND_H
#define __ESP_WIFI_SEND_H

#include "stm32f4xx.h"
#include <stdint.h>

/* 发送结果 */
#define WIFI_SEND_OK        0   /* 收到期望应答 */
#define WIFI_SEND_OVERTIME  1   /* 重发次数耗尽 */
#define WIFI_SEND_ERROR     2   /* 收到失败应答 */

/* AT指令ID（对应wifi_pro的esp_at.h） */
typedef enum {
    WIFI_CMD_AT = 0,            /* 模块联通测试 */
    WIFI_CMD_CWMODE,            /* 模式设置：AP+STA */
    WIFI_CMD_CIPMUX,            /* 多连接使能 */
    WIFI_CMD_CWJAP,             /* 连接路由器 */
    WIFI_CMD_CIPSTART_UDP,      /* 建立UDP链路(链路0) */
    WIFI_CMD_CIPSEND,           /* 链路0发送请求 */
    WIFI_CMD_CIPCLOSE,          /* 关闭链路0 */
    WIFI_CMD_NUM
} WIFI_CMD_E;

/* AT命令表项（对应wifi_pro的wifi_at_cmd_t） */
typedef struct {
    const char *tpl;            /* 指令字符串 */
    uint16_t timeout_ms;        /* 应答超时 */
    uint8_t retries;            /* 额外重试次数 */
    const char *expect;         /* 期望应答特征 */
    const char *fail;           /* 失败应答特征，NULL表示无 */
} WIFI_AT_CMD_T;

/* 发送结果回调 */
typedef void (*WIFI_SEND_DONE_CB)(uint8_t result);

/*******************************************************************
** 函数名	: WIFI_SendInit
** 函数描述	: AT指令发送模块初始化：节点池与链表建立
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_SendInit(void);

/*******************************************************************
** 函数名	: WIFI_SendCmdExec
** 函数描述	: 按命令ID从命令表取参数执行发送，结果回调通知
** 参数		: [in] id: 命令ID(WIFI_CMD_E)
**          : [in] fp: 发送结果回调
** 返回		: 1入队成功，0失败
********************************************************************/
uint8_t WIFI_SendCmdExec(WIFI_CMD_E id, WIFI_SEND_DONE_CB fp);

/*******************************************************************
** 函数名	: WIFI_SendListSend
** 函数描述	: 组装AT指令入发送队列，超时自动重发，结果回调通知
** 参数		: [in] expect: 期望应答特征("OK"/">")
**          : [in] fail: 失败应答特征，NULL表示无
**          : [in] ptr: AT指令字符串
**          : [in] len: 指令长度
**          : [in] ct_send: 总发送次数(重试+1)
**          : [in] ct_time_ms: 应答超时(ms)
**          : [in] fp: 发送结果回调
** 返回		: 1入队成功，0失败
********************************************************************/
uint8_t WIFI_SendListSend(const char *expect, const char *fail, const char *ptr,
                          uint16_t len, uint8_t ct_send, uint16_t ct_time_ms,
                          WIFI_SEND_DONE_CB fp);

/*******************************************************************
** 函数名	: WIFI_SendAck
** 函数描述	: 接收侧应答匹配：通知等待队列头的指令发送结果
** 参数		: [in] feature: 应答特征("OK"/"ERROR"/"FAIL"/">")
** 返回		: 无
********************************************************************/
void WIFI_SendAck(const char *feature);

#endif /* __ESP_WIFI_SEND_H */
