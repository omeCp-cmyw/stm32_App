#include <stdio.h>
#include <string.h>
#include "drv_uart.h"
#include "os_include.h"
#include "tool_list.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_send.h"

/*
 * 发送模块参考yx_mmi_send.c框架：
 * 指令节点池 + 等待/就绪/空闲三链表 + 周期定时器调度，
 * 等待应答超时自动重发，接收侧WIFI_SendAck匹配应答触发结果回调
 */

#define NUM_MEM         4
#define PERIOD_SEND     _MILTICK, 10

/*
 * AT命令表（参考wifi_pro的esp_at.c s_cmd_tbl）：
 * 每条指令独立配置应答超时、额外重试次数、期望/失败应答特征
 */
static const WIFI_AT_CMD_T s_cmd_tbl[WIFI_CMD_NUM] = {
    [WIFI_CMD_AT]           = { "AT\r\n",                                             5000,  2, "OK",    "ERROR" },
    [WIFI_CMD_CWMODE]       = { "AT+CWMODE=3\r\n",                                     5000,  1, "OK",    "ERROR" },
    [WIFI_CMD_CIPMUX]       = { "AT+CIPMUX=1\r\n",                                     5000,  1, "OK",    "ERROR" },
    [WIFI_CMD_CWJAP]        = { "AT+CWJAP=\""WIFI_SSID"\",\""WIFI_PASS"\"\r\n",      15000, 1, "OK",    "FAIL"  },
    [WIFI_CMD_CIPSTART_UDP] = { "AT+CIPSTART=0,\"UDP\",\""WIFI_NTP_SERVER"\",123\r\n", 10000, 1, "OK",  "ERROR" },
    [WIFI_CMD_CIPSEND]      = { "AT+CIPSEND=0,48\r\n",                                 5000,  1, ">",     "ERROR" },
    [WIFI_CMD_CIPCLOSE]     = { "AT+CIPCLOSE=0\r\n",                                   5000,  0, "OK",    NULL    },
};

typedef struct {
    const char *str;            /* AT指令字符串 */
    uint16_t len;               /* 指令长度 */
    const char *expect;         /* 期望应答特征 */
    const char *fail;           /* 失败应答特征 */
    uint8_t ct_send;            /* 剩余发送次数 */
    uint16_t flowtime;          /* 应答超时(10ms计) */
    uint16_t ct_time;           /* 等待计时 */
    WIFI_SEND_DONE_CB fp;       /* 发送结果回调 */
} WIFI_CELL_T;

static struct {
    NODE_T reserve;
    WIFI_CELL_T cell;
} s_memory[NUM_MEM];

static INT8U s_sendtmr;
static LIST_T s_waitlist, s_readylist, s_freelist;

/*******************************************************************
** 函数名	: WifiDelCell
** 函数描述	: 删除节点归还空闲链表，并触发发送结果回调
** 参数		: [in] cell: 指令节点
**          : [in] result: 发送结果
** 返回		: 无
********************************************************************/
static void WifiDelCell(WIFI_CELL_T *cell, uint8_t result)
{
    void (*fp)(uint8_t result);

    fp = cell->fp;
    LS_LIST_AppendListEle(&s_freelist, (INT8U *)cell);
    if (fp != 0) {
        fp(result);
    }
}

/*******************************************************************
** 函数名	: WifiSendTmrProc
** 函数描述	: 发送扫描定时器：等待队列应答超时移就绪重发，
**			就绪队列到期发送，无节点时停止定时器
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void WifiSendTmrProc(void *pdata)
{
    WIFI_CELL_T *cell, *next;

    pdata = pdata;

    if (LS_LIST_GetNodeNum(&s_waitlist) + LS_LIST_GetNodeNum(&s_readylist) == 0) {
        OS_StopTmr(s_sendtmr);
        return;
    }
    OS_StartTmr(s_sendtmr, PERIOD_SEND);

    /* 扫描等待队列：应答超时则重发 */
    cell = (WIFI_CELL_T *)LS_LIST_GetListHead(&s_waitlist);
    for (;;) {
        if (cell == 0) break;
        if (++cell->ct_time > cell->flowtime) {
            cell->ct_time = 0;
            next = (WIFI_CELL_T *)LS_LIST_DeleListEle(&s_waitlist, (INT8U *)cell);
            if (--cell->ct_send == 0) {
                /* 重发次数耗尽 */
                WifiDelCell(cell, WIFI_SEND_OVERTIME);
            } else {
                LS_LIST_AppendListEle(&s_readylist, (INT8U *)cell);
            }
            cell = next;
        } else {
            cell = (WIFI_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);
        }
    }

    /* 扫描就绪队列：等待时间到则发送 */
    cell = (WIFI_CELL_T *)LS_LIST_GetListHead(&s_readylist);
    for (;;) {
        if (cell == 0) break;
        if (++cell->ct_time > cell->flowtime) {
            cell->ct_time = 0;
            if (cell->len > DRV_UART_LeftOfSendbuf(WIFI_COM)) {
                /* 发送缓冲不足，下轮再发 */
                cell = (WIFI_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);
                continue;
            }
            next = (WIFI_CELL_T *)LS_LIST_DeleListEle(&s_readylist, (INT8U *)cell);
            printf("[wifi] send: %s", cell->str);
            DRV_UART_WriteBlock(WIFI_COM, (INT8U *)cell->str, cell->len);
            if (cell->ct_send > 0) {
                /* 需等待应答，移入等待队列 */
                LS_LIST_AppendListEle(&s_waitlist, (INT8U *)cell);
            } else {
                WifiDelCell(cell, WIFI_SEND_OK);
            }
            cell = next;
        } else {
            cell = (WIFI_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);
        }
    }
}

/*******************************************************************
** 函数名	: WIFI_SendInit
** 函数描述	: AT指令发送模块初始化：节点池与链表建立
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_SendInit(void)
{
    LS_LIST_Init(&s_waitlist);
    LS_LIST_Init(&s_readylist);
    LS_LIST_CreateList(&s_freelist, (INT8U *)s_memory, NUM_MEM, sizeof(s_memory[0]));

    s_sendtmr = OS_CreateTmr((void *)0, WifiSendTmrProc);
}

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
                          WIFI_SEND_DONE_CB fp)
{
    WIFI_CELL_T *cell;

    if (ptr == 0 || len == 0) {
        return 0;
    }
    if ((cell = (WIFI_CELL_T *)LS_LIST_DeleListHead(&s_freelist)) != 0) {
        cell->str      = ptr;
        cell->len      = len;
        cell->expect   = expect;
        cell->fail     = fail;
        cell->ct_send  = ct_send;
        cell->flowtime = ct_time_ms / 10;
        if (cell->flowtime == 0) {
            cell->flowtime = 1;
        }
        cell->ct_time = cell->flowtime;     /* 首次发送立即触发 */
        cell->fp      = fp;
        LS_LIST_AppendListEle(&s_readylist, (INT8U *)cell);

        if (!OS_TmrIsRun(s_sendtmr)) {
            OS_StartTmr(s_sendtmr, PERIOD_SEND);
        }
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: WIFI_SendCmdExec
** 函数描述	: 按命令ID从命令表取参数执行发送，结果回调通知
** 参数		: [in] id: 命令ID(WIFI_CMD_E)
**          : [in] fp: 发送结果回调
** 返回		: 1入队成功，0失败
********************************************************************/
uint8_t WIFI_SendCmdExec(WIFI_CMD_E id, WIFI_SEND_DONE_CB fp)
{
    const WIFI_AT_CMD_T *cfg;

    if (id >= WIFI_CMD_NUM) {
        return 0;
    }
    cfg = &s_cmd_tbl[id];
    /* ct_send = 额外重试次数 + 1（首次发送） */
    return WIFI_SendListSend(cfg->expect, cfg->fail, cfg->tpl,
                             (uint16_t)strlen(cfg->tpl), cfg->retries + 1,
                             cfg->timeout_ms, fp);
}

/*******************************************************************
** 函数名	: WIFI_SendAck
** 函数描述	: 接收侧应答匹配：通知等待队列头的指令发送结果
** 参数		: [in] feature: 应答特征("OK"/"ERROR"/"FAIL"/">")
** 返回		: 无
********************************************************************/
void WIFI_SendAck(const char *feature)
{
    WIFI_CELL_T *cell;

    if (feature == 0) {
        return;
    }
    cell = (WIFI_CELL_T *)LS_LIST_GetListHead(&s_waitlist);
    if (cell == 0) {
        return;
    }
    if (cell->fail != 0 && strcmp(feature, cell->fail) == 0) {
        /* 收到失败应答 */
        LS_LIST_DeleListEle(&s_waitlist, (INT8U *)cell);
        WifiDelCell(cell, WIFI_SEND_ERROR);
        return;
    }
    if (cell->expect != 0 && strcmp(feature, cell->expect) == 0) {
        /* 收到期望应答 */
        LS_LIST_DeleListEle(&s_waitlist, (INT8U *)cell);
        WifiDelCell(cell, WIFI_SEND_OK);
    }
}
