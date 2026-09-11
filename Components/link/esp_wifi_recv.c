#include <stdio.h>
#include <string.h>
#include "drv_uart.h"
#include "drv_systick.h"
#include "osal.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_recv.h"
#include "esp_wifi_send.h"

/*
 * 接收模块：
 * 周期扫描定时器读串口字节，逐字节状态机解析，
 * 文本行匹配应答特征并通知发送模块，
 * +IPD,<link>,<len>:二进制负载按链路投递注册回调
 */

/* 接收扫描周期：改用osal_timer_start毫秒接口，10ms */
#define PERIOD_SCAN_MS      10
#define LINE_BUF_SIZE   64
#define IPD_BUF_SIZE    2560        /* +IPD负载缓冲：MQTT帧上限+OTA HTTP响应 */
#define IPD_LINK_MAX    5
#define IPD_TIMEOUT_MS  500         /* RS_IPD未凑满超时，强制投递防吞AT回显 */

/* 接收状态 */
typedef enum {
    RS_LINE = 0,                /* 文本行模式 */
    RS_IPD                      /* +IPD二进制负载模式 */
} WIFI_RS_E;

static INT8U s_scantmr;
static WIFI_RS_E s_rs;
static char s_line[LINE_BUF_SIZE];
static INT8U s_line_len;
static INT8U s_ipd_buf[IPD_BUF_SIZE];
static INT16U s_ipd_cnt;
static INT16U s_ipd_dlen;       /* 本片负载总长(+IPD头声明) */
static INT8U s_ipd_link;
static INT32U s_ipd_tick;       /* 进入RS_IPD的时间戳(ms) */
static WIFI_RECV_IPD_CB s_ipd_cb[IPD_LINK_MAX];
static WIFI_RECV_CLOSE_CB s_close_cb[IPD_LINK_MAX];

/*******************************************************************
** 函数名	: WifiIpdDeliver
** 函数描述	: +IPD负载凑满后打印字符串形式并按链路投递注册回调
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiIpdDeliver(void)
{
#if 0
    /* 字符串形式打印IPD负载(可选调试) */
    INT16U i;
    INT8U c;
    printf("[wifi] ipd str(%u): ", (unsigned int)s_ipd_cnt);
    for (i = 0; i < s_ipd_cnt; i++) {
        c = s_ipd_buf[i];
        printf("%c", (c >= 0x20 && c <= 0x7e) ? (char)c : '.');
    }
    printf("\r\n");
#endif

    if (s_ipd_link < IPD_LINK_MAX && s_ipd_cb[s_ipd_link] != 0) {
        s_ipd_cb[s_ipd_link](s_ipd_link, s_ipd_buf, s_ipd_cnt);
    } else {
        printf("[wifi] ipd link %u no handler, %u bytes dropped\r\n",
               (unsigned int)s_ipd_link, (unsigned int)s_ipd_cnt);
    }
    s_ipd_cnt = 0;
    s_ipd_dlen = 0;
}

/*******************************************************************
** 函数名	: WifiIpdHeadParse
** 函数描述	: 解析"+IPD,<link>,<len>:"头，冒号后字节作为负载开头
** 参数		: 无
** 返回		: 1切负载模式，0头未完整/非+IPD行
** 备注		: 检测到+IPD前缀后持续累积直到冒号，避免二进制数据中\n误触发行匹配
********************************************************************/
static INT8U WifiIpdHeadParse(void)
{
    char *colon;
    char *p;
    char *ipd;
    INT32S ipd_len = 0;
    INT8U tail;

    /* 在行缓冲中搜索+IPD头：容忍前缀垃圾字节(ESP8266残留数据)，
       否则strncmp行首匹配失败会丢整包负载 */
    ipd = strstr(s_line, "+IPD,");
    if (ipd == 0) {
        return 0;
    }
    if (ipd != s_line) {
        INT16U skip = (INT16U)(ipd - s_line);
        printf("[wifi] ipd resync, drop %u junk bytes\r\n", (unsigned int)skip);
        memmove(s_line, ipd, s_line_len - skip);
        s_line_len = (INT8U)(s_line_len - skip);
    }
    if (s_line_len < 9) {
        return 0;
    }
    /* 链路号：+IPD,<link>, */
    if (s_line[5] < '0' || s_line[5] > '4' || s_line[6] != ',') {
        return 0;
    }
    s_ipd_link = (INT8U)(s_line[5] - '0');

    /* 负载长度：数字到冒号 */
    colon = strchr(s_line + 7, ':');
    if (colon == 0) {
        /* 未找到冒号：可能是+IPD头还没收完整，继续等待 */
        return 0;
    }
    p = s_line + 7;
    while (p < colon) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        ipd_len = ipd_len * 10 + (*p - '0');
        if (ipd_len > IPD_BUF_SIZE) {
            /* 声明长度超过缓冲上限：不可能凑满，拒绝防止死锁 */
            return 0;
        }
        p++;
    }
    if (ipd_len <= 0) {
        return 0;
    }

    /* 冒号后已到达的字节是负载开头 */
    tail = (INT8U)(s_line_len - (INT32U)(colon - s_line) - 1);
    if (tail > 0) {
        if (s_ipd_cnt + tail <= IPD_BUF_SIZE) {
            memcpy(s_ipd_buf + s_ipd_cnt, colon + 1, tail);
            s_ipd_cnt += tail;
        } else {
            printf("[wifi] ipd overflow, %u bytes dropped\r\n", (unsigned int)tail);
        }
    }
    s_ipd_dlen = (INT16U)ipd_len;
    s_line_len = 0;
    s_line[0]  = 0;
    return 1;
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
        /* +IPD二进制负载：按头声明长度累积 */
        if (s_ipd_cnt < s_ipd_dlen && s_ipd_cnt < IPD_BUF_SIZE) {
            s_ipd_buf[s_ipd_cnt++] = rdata;
        }
        if (s_ipd_cnt >= s_ipd_dlen) {
            /* 本片收齐，投递解析 */
            WifiIpdDeliver();
            s_rs = RS_LINE;
        }
        return;
    }

    /* 文本行模式 */
    if (rdata == '\r') {
        return;
    }
    if (rdata == '\n') {
        /* 检测到+IPD前缀但未收完整头时，跳过换行符继续累积 */
        if (s_line_len >= 5 && strncmp(s_line, "+IPD,", 5) == 0) {
            /* +IPD头可能跨越多行，继续等待冒号 */
            if (s_line_len < sizeof(s_line) - 1) {
                s_line[s_line_len++] = (char)rdata;
            }
            return;
        }
        /* 一行结束，判断应答 */
        if (s_line_len > 0) {
            s_line[s_line_len] = 0;
            printf("[wifi] %s\r\n", s_line);
            if (strstr(s_line, "WIFI DISCONNECT") != 0) {
                /* STA断开事件，上报mmi记录状态并触发恢复 */
                WIFI_MMI_OnStaEvent(0);
            } else if (strstr(s_line, "WIFI CONNECTED") != 0) {
                /* STA已连接事件，上报mmi记录状态 */
                WIFI_MMI_OnStaEvent(1);
            } else if (strstr(s_line, ",CLOSED") != 0) {
                /* "<link>,CLOSED"链路关闭事件，按链路投递回调 */
                if (s_line_len >= 2 && s_line[0] >= '0' && s_line[0] <= '4' && s_line[1] == ',') {
                    INT8U cl = (INT8U)(s_line[0] - '0');
                    if (s_close_cb[cl] != 0) {
                        s_close_cb[cl](cl);
                    }
                }
            } else if (strstr(s_line, "SEND OK") != 0) {
                WIFI_SendAck("SEND OK");
            } else if (strstr(s_line, "SEND FAIL") != 0) {
                WIFI_SendAck("SEND FAIL");
            } else if (strstr(s_line, "ALREADY CONNECTED") != 0) {
                /* CIPSTART时链路已存在，视为成功 */
                WIFI_SendAck("OK");
            } else if (strstr(s_line, "busy p") != 0) {
                /* ESP8266忙，标记发送失败 */
                printf("[wifi] busy p detected\r\n");
                WIFI_SendAck("BUSY");
            } else if (strstr(s_line, "ERROR") != 0) {
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
        return;
    }
    /* +IPD头检测，切换二进制负载模式 */
    if (WifiIpdHeadParse() != 0) {
        if (s_ipd_cnt >= s_ipd_dlen) {
            WifiIpdDeliver();
        } else {
            s_rs = RS_IPD;
            s_ipd_tick = SYSTICK_GetMsTick();
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

    /* RS_IPD超时保护：+IPD声明长度与实收不符时会吞掉后续AT回显，
       超时未凑满则强制投递已收数据并恢复行模式 */
    if (s_rs == RS_IPD && s_ipd_cnt > 0 &&
        SYSTICK_GetMsTick() - s_ipd_tick >= IPD_TIMEOUT_MS) {
        printf("[wifi] ipd timeout: got %u/%u, force deliver\r\n",
               (unsigned int)s_ipd_cnt, (unsigned int)s_ipd_dlen);
        /* 诊断：打印超时投递数据前32字节(疑似垃圾区)和尾16字节(疑似真实区) */
        {
            INT16U i, n = s_ipd_cnt;
            printf("[wifi] ipd head:");
            for (i = 0; i < n && i < 32; i++) {
                printf(" %02X", s_ipd_buf[i]);
            }
            printf("\r\n[wifi] ipd tail:");
            for (i = (n > 16) ? n - 16 : 0; i < n; i++) {
                printf(" %02X", s_ipd_buf[i]);
            }
            printf("\r\n");
        }
        WifiIpdDeliver();
        s_rs = RS_LINE;
    }

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
    s_rs       = RS_LINE;
    s_line_len = 0;
    s_ipd_cnt  = 0;
    s_ipd_dlen = 0;
    s_ipd_link = 0;
    memset(s_ipd_cb, 0, sizeof(s_ipd_cb));
    memset(s_close_cb, 0, sizeof(s_close_cb));

    s_scantmr = osal_timer_create((void *)0, WifiRecvTmrProc);
    if (s_scantmr != 0xff) {
        osal_timer_start(s_scantmr, PERIOD_SCAN_MS, 1);
    }
}

/*******************************************************************
** 函数名	: WIFI_RecvSetIpdCb
** 函数描述	: 注册指定链路的+IPD负载处理回调，收到该链路数据时调用
** 参数		: [in] link: 链路号(0~4)
**          : [in] cb: 负载回调，NULL取消注册
** 返回		: 无
********************************************************************/
void WIFI_RecvSetIpdCb(uint8_t link, WIFI_RECV_IPD_CB cb)
{
    if (link < IPD_LINK_MAX) {
        s_ipd_cb[link] = cb;
    }
}

/*******************************************************************
** 函数名	: WIFI_RecvSetCloseCb
** 函数描述	: 注册指定链路的关闭事件回调，收到"<link>,CLOSED"时调用
** 参数		: [in] link: 链路号(0~4)
**          : [in] cb: 关闭回调，NULL取消注册
** 返回		: 无
********************************************************************/
void WIFI_RecvSetCloseCb(uint8_t link, WIFI_RECV_CLOSE_CB cb)
{
    if (link < IPD_LINK_MAX) {
        s_close_cb[link] = cb;
    }
}
