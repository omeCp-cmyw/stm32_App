#include <stdio.h>
#include <string.h>
#include "drv_uart.h"
#include "os_include.h"
#include "ntp_mmi.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_recv.h"
#include "esp_wifi_send.h"

/*
 * 接收模块参考yx_mmi_recv.c框架：
 * 周期扫描定时器读串口字节，逐字节状态机解析，
 * 文本行匹配OK/ERROR/'>'应答并通知发送模块，
 * +IPD二进制负载累积后投递注册回调
 */

#define PERIOD_SCAN     _MILTICK, 10
#define LINE_BUF_SIZE   64

/* 接收状态 */
typedef enum {
    RS_LINE = 0,                /* 文本行模式 */
    RS_IPD                      /* +IPD二进制负载模式 */
} WIFI_RS_E;

static INT8U s_scantmr;
static WIFI_RS_E s_rs;
static char s_line[LINE_BUF_SIZE];
static INT8U s_line_len;
static INT8U s_ipd_buf[NTP_MMI_PACKET_SIZE];
static INT8U s_ipd_cnt;
static INT16U s_ipd_remain;
static WIFI_RECV_IPD_CB s_ipd_cb;

/*******************************************************************
** 函数名	: WifiIpdDeliver
** 函数描述	: +IPD负载凑满后投递注册回调
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiIpdDeliver(void)
{
    if (s_ipd_cb != 0) {
        s_ipd_cb(s_ipd_buf, s_ipd_cnt);
    }
    s_ipd_cnt = 0;
}

/*******************************************************************
** 函数名	: WifiRecvHandle
** 函数描述	: 接收字节处理：行匹配应答特征，提取+IPD负载
** 参数		: [in] rdata: 接收字节
** 返回		: 无
********************************************************************/
static void WifiRecvHandle(INT8U rdata)
{
    if (s_rs == RS_IPD) {
        /* +IPD二进制负载：累积到缓冲 */
        if (s_ipd_cnt < NTP_MMI_PACKET_SIZE) {
            s_ipd_buf[s_ipd_cnt++] = rdata;
        }
        if (s_ipd_remain > 0) {
            s_ipd_remain--;
        }
        if (s_ipd_cnt >= NTP_MMI_PACKET_SIZE) {
            /* 凑满48字节，投递解析 */
            WifiIpdDeliver();
            s_rs = RS_LINE;
        } else if (s_ipd_remain == 0) {
            /* 本片结束，回行模式等下一片+IPD头 */
            s_rs = RS_LINE;
        }
        return;
    }

    /* 文本行模式 */
    if (rdata == '\r') {
        return;
    }
    if (rdata == '\n') {
        /* 一行结束，判断应答 */
        if (s_line_len > 0) {
            s_line[s_line_len] = 0;
            printf("[wifi] %s\r\n", s_line);
            if (strstr(s_line, "ERROR") != 0) {
                WIFI_SendAck("ERROR");
            } else if (strstr(s_line, "FAIL") != 0) {
                WIFI_SendAck("FAIL");
            } else if (strstr(s_line, "OK") != 0) {
                WIFI_SendAck("OK");
            }
            s_line_len = 0;
        }
        return;
    }
    if (s_line_len < sizeof(s_line) - 1) {
        s_line[s_line_len++] = (char)rdata;
    }
    /* '>'提示符不带换行，收到即触发 */
    if (rdata == '>') {
        WIFI_SendAck(">");
    }
    /* +IPD头检测："+IPD,0,<len>:"，切二进制负载模式 */
    if (s_line_len >= 9 && strncmp(s_line, "+IPD,0,", 7) == 0) {
        char *colon = strchr(s_line + 7, ':');
        if (colon != 0) {
            INT32S ipd_len = 0;
            char *p = s_line + 7;
            INT8U tail;
            while (p < colon) {
                if (*p < '0' || *p > '9') {
                    ipd_len = -1;
                    break;
                }
                ipd_len = ipd_len * 10 + (*p - '0');
                p++;
            }
            if (ipd_len > 0) {
                /* 冒号后已到达的字节是负载开头 */
                tail = (INT8U)(s_line_len - (INT32U)(colon - s_line) - 1);
                if (tail > 0 && s_ipd_cnt < NTP_MMI_PACKET_SIZE) {
                    memcpy(s_ipd_buf + s_ipd_cnt, colon + 1, tail);
                    s_ipd_cnt += tail;
                }
                s_ipd_remain = (INT16U)ipd_len - tail;
                s_line_len = 0;
                s_line[0] = 0;
                if (s_ipd_cnt >= NTP_MMI_PACKET_SIZE) {
                    WifiIpdDeliver();
                } else if (s_ipd_remain > 0) {
                    s_rs = RS_IPD;
                }
            }
        }
    }
}

/*******************************************************************
** 函数名	: WifiRecvTmrProc
** 函数描述	: 接收扫描定时器：读USART2字节循环喂入解析
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void WifiRecvTmrProc(void *pdata)
{
    INT32S recv;

    pdata = pdata;

    for (;;) {
        if ((recv = DRV_UART_ReadChar(WIFI_COM)) == -1) {
            break;
        }
        WifiRecvHandle((INT8U)recv);
    }
}

/*******************************************************************
** 函数名	: WIFI_RecvInit
** 函数描述	: 接收模块初始化：启动接收扫描定时器
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_RecvInit(void)
{
    s_rs         = RS_LINE;
    s_line_len   = 0;
    s_ipd_cnt    = 0;
    s_ipd_remain = 0;
    s_ipd_cb     = 0;

    s_scantmr = OS_CreateTmr((void *)0, WifiRecvTmrProc);
    if (s_scantmr != 0xff) {
        OS_StartTmr(s_scantmr, PERIOD_SCAN);
    }
}

/*******************************************************************
** 函数名	: WIFI_RecvSetIpdCb
** 函数描述	: 注册+IPD负载处理回调，收到链路0数据时调用
** 参数		: [in] cb: 负载回调
** 返回		: 无
********************************************************************/
void WIFI_RecvSetIpdCb(WIFI_RECV_IPD_CB cb)
{
    s_ipd_cb = cb;
}
