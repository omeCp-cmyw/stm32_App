#include <stdio.h>
#include "drv_uart.h"
#include "osal.h"
#include "ntp_mmi.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_send.h"
#include "esp_wifi_recv.h"

/*
 * 流程编排采用定时器状态机框架：
 * 复位时序由状态机定时器驱动，AT指令序列由发送模块结果回调驱动，
 * NTP校时完成后关闭UDP链路，初始化结束
 */

/* WiFi复位脚：PG15，低电平有效 */
#define WIFI_RST_PORT           GPIOG
#define WIFI_RST_PIN            GPIO_PIN_15

/* NTP配置 */
#define NTP_TIMEOUT             1000    /* +IPD应答超时：1000x10ms=10s */
#define NTP_TRIES               3       /* NTP请求重发次数 */

/*
 * 初始化指令序列：按命令ID依次执行，
 * 指令的字符串/超时/重试/应答特征由esp_wifi_send.c命令表配置
 */
typedef struct {
    WIFI_CMD_E cmd;             /* 命令ID */
    uint16_t gap_ms;            /* 收到OK后到下一条的间隔 */
} WIFI_SEQ_T;

static const WIFI_SEQ_T s_init_seq[] = {
    { WIFI_CMD_AT,          0    },
    { WIFI_CMD_ATE0,        0    },   /* 关闭回显，防止数据污染 */
    { WIFI_CMD_CWMODE,      0    },
    { WIFI_CMD_CIPMUX,      0    },
    { WIFI_CMD_CWJAP,       1000 },   /* 连路由后等模块拿IP */
    { WIFI_CMD_CIPSTART_UDP, 0   },
};

#define WIFI_SEQ_NUM            (sizeof(s_init_seq) / sizeof(s_init_seq[0]))

/* 流程步骤 */
typedef enum {
    STEP_RESET_LOW = 0,
    STEP_RESET_HIGH,
    STEP_SEND_AT,               /* 复位完成，开始发送AT表 */
    STEP_DELAY,                 /* 指令间隔延时 */
    STEP_NTP_WAIT,              /* 等+IPD应答 */
    STEP_WIFI_LOST,             /* 掉线延时，到点按s_ready重连或复位重来 */
    STEP_END
} WIFI_STEP_E;

static INT8U s_wifitmr;
static WIFI_STEP_E s_step;
static uint8_t s_cur;           /* 当前指令索引 */
static uint8_t s_ntp_retry;     /* NTP请求已重发次数 */
static INT16U s_ntp_poll;       /* NTP应答轮询计数 */
static uint8_t s_ready;
static uint8_t s_ntp_req[NTP_MMI_PACKET_SIZE];
static WIFI_STA_E s_sta;        /* STA连接状态记录 */

/* 前置声明 */
static void WifiSendCur(void);
static void WifiCipsendDone(uint8_t result);
static void WifiRestart(void);

/*******************************************************************
** 函数名	: WifiStaName
** 函数描述	: STA状态枚举转可打印名称
** 参数		: [in] sta: 状态枚举
** 返回		: 状态名称字符串
********************************************************************/
static const char *WifiStaName(WIFI_STA_E sta)
{
    switch (sta) {
    case WIFI_STA_INIT:       return "init";
    case WIFI_STA_CONNECTING: return "connecting";
    case WIFI_STA_CONNECTED:  return "connected";
    case WIFI_STA_LOST:       return "lost";
    default:                  return "fail";
    }
}

/*******************************************************************
** 函数名	: WifiStaSet
** 函数描述	: 更新STA状态记录，变化时打印日志
** 参数		: [in] sta: 新状态
** 返回		: 无
********************************************************************/
static void WifiStaSet(WIFI_STA_E sta)
{
    if (s_sta != sta) {
        s_sta = sta;
        printf("[wifi] sta: %s\r\n", WifiStaName(sta));
    }
}

/*******************************************************************
** 函数名	: WifiRestart
** 函数描述	: 复位初始化上下文并整体重走复位初始化流程
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiRestart(void)
{
    WifiStaSet(WIFI_STA_CONNECTING);
    s_cur       = 0;
    s_ntp_retry = 0;
    s_ready     = 0;
    s_step      = STEP_RESET_LOW;
    osal_timer_start(s_wifitmr, 1, 1);
}

/*******************************************************************
** 函数名	: WifiGpioInit
** 函数描述	: PG15复位脚初始化为推挽输出，默认高电平(不复位)
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiGpioInit(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = WIFI_RST_PIN;
    HAL_GPIO_Init(WIFI_RST_PORT, &gpio);

    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
}

/*******************************************************************
** 函数名	: WifiCloseDone
** 函数描述	: CIPCLOSE发送结果回调：初始化流程结束
** 参数		: [in] result: 发送结果(未用)
** 返回		: 无
********************************************************************/
static void WifiCloseDone(uint8_t result)
{
    result = result;

    s_ready = 1;
    s_step  = STEP_END;
    printf("wifi init done, ntp %s\r\n", NTP_MMI_TimeValid() ? "ok" : "fail");
}

/*******************************************************************
** 函数名	: WifiReconnDone
** 函数描述	: 运行中掉线后重连CWJAP发送结果回调：
**			成功更新状态并停止流程，失败延时重试
** 参数		: [in] result: 发送结果
** 返回		: 无
********************************************************************/
static void WifiReconnDone(uint8_t result)
{
    if (result == WIFI_SEND_OK) {
        WifiStaSet(WIFI_STA_CONNECTED);
        s_step = STEP_END;
        osal_timer_stop(s_wifitmr);
    } else {
        s_step = STEP_WIFI_LOST;
        osal_timer_start(s_wifitmr, 5000, 1);
    }
}

/*******************************************************************
** 函数名	: WifiIpdHandler
** 函数描述	: +IPD负载处理回调：打印原始数据，解析NTP应答建立时间基准，
**			成功后关闭UDP链路
** 参数		: [in] link: 链路号(未用，固定注册链路0)
**          : [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
static void WifiIpdHandler(uint8_t link, const uint8_t *data, uint16_t len)
{
    uint32_t unix_ts;
    uint16_t i;

    link = link;

    /* 打印NTP应答原始数据 */
    printf("[wifi] ntp raw(%u):\r\n", (unsigned int)len);
    for (i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i & 0xf) == 0xf) {
            printf("\r\n");
        }
    }
    if ((len & 0xf) != 0) {
        printf("\r\n");
    }

    if (NTP_MMI_ParseReply(data, len, &unix_ts) == 0) {
        NTP_MMI_SetTime(unix_ts);
        /* 时间获取成功，立即关闭UDP链路 */
        osal_timer_stop(s_wifitmr);
        WIFI_SendCmdExec(WIFI_CMD_CIPCLOSE, WifiCloseDone);
    } else {
        printf("[wifi] ntp reply invalid\r\n");
    }
}

/*******************************************************************
** 函数名	: WifiATCmdDone
** 函数描述	: AT表指令发送结果回调：成功发下一条，失败结束流程
** 参数		: [in] result: 发送结果
** 返回		: 无
********************************************************************/
static void WifiATCmdDone(uint8_t result)
{
    if (result == WIFI_SEND_OK) {
        if (s_init_seq[s_cur].cmd == WIFI_CMD_CWJAP) {
            /* 连路由成功，记录状态 */
            WifiStaSet(WIFI_STA_CONNECTED);
        }
        s_cur++;
        if (s_cur < WIFI_SEQ_NUM) {
            if (s_init_seq[s_cur].gap_ms > 0) {
                /* 延时后发送下一条 */
                s_step = STEP_DELAY;
                osal_timer_start(s_wifitmr, s_init_seq[s_cur].gap_ms, 1);
            } else {
                WifiSendCur();
            }
        } else {
            /* 基础指令完成，发起CIPSEND等'>'提示 */
            WIFI_SendCmdExec(WIFI_CMD_CIPSEND, WifiCipsendDone);
        }
    } else {
        printf("wifi init fail: cmd %u\r\n", (unsigned int)s_cur);
        if (s_init_seq[s_cur].cmd == WIFI_CMD_CWJAP) {
            /* 连路由失败，延时后复位重试整个初始化 */
            WifiStaSet(WIFI_STA_FAIL);
            s_step = STEP_WIFI_LOST;
            osal_timer_start(s_wifitmr, 5000, 1);
        } else {
            s_step = STEP_END;
        }
    }
}

/*******************************************************************
** 函数名	: WifiCipsendDone
** 函数描述	: CIPSEND发送结果回调：收到'>'后发送48字节NTP请求
** 参数		: [in] result: 发送结果
** 返回		: 无
********************************************************************/
static void WifiCipsendDone(uint8_t result)
{
    if (result != WIFI_SEND_OK) {
        printf("wifi init fail: cipsend\r\n");
        s_step = STEP_END;
        return;
    }
    NTP_MMI_BuildRequest(s_ntp_req);
    DRV_UART_WriteBlock(WIFI_COM, s_ntp_req, NTP_MMI_PACKET_SIZE);

    s_ntp_retry = 0;
    s_ntp_poll  = 0;
    s_step = STEP_NTP_WAIT;
    osal_timer_start(s_wifitmr, 10, 1);
}

/*******************************************************************
** 函数名	: WifiSendCur
** 函数描述	: 发送AT指令表当前指令
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiSendCur(void)
{
    if (s_init_seq[s_cur].cmd == WIFI_CMD_CWJAP) {
        WifiStaSet(WIFI_STA_CONNECTING);
    }
    WIFI_SendCmdExec(s_init_seq[s_cur].cmd, WifiATCmdDone);
}

/*******************************************************************
** 函数名	: WifiTmrProc
** 函数描述	: 定时器回调，状态机步进：复位时序、指令间隔延时、
**			NTP应答超时重发与收尾
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void WifiTmrProc(void *pdata)
{
    pdata = pdata;

    switch (s_step) {
    case STEP_RESET_LOW:
        /* 复位脚拉低，复位WiFi模块 */
        HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
        s_step = STEP_RESET_HIGH;
        osal_timer_start(s_wifitmr, 200, 1);
        break;

    case STEP_RESET_HIGH:
        /* 拉高复位脚，等模块重启完成 */
        HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
        s_step = STEP_SEND_AT;
        osal_timer_start(s_wifitmr, 2000, 1);
        break;

    case STEP_SEND_AT:
        /* 开始发送AT指令表第一条 */
        osal_timer_stop(s_wifitmr);
        s_cur = 0;
        WifiSendCur();
        break;

    case STEP_DELAY:
        osal_timer_stop(s_wifitmr);
        WifiSendCur();
        break;

    case STEP_NTP_WAIT:
        if (++s_ntp_poll >= NTP_TIMEOUT) {
            s_ntp_poll = 0;
            if (++s_ntp_retry < NTP_TRIES) {
                /* 超时重发NTP请求 */
                printf("[wifi] ntp timeout, retry %u\r\n", (unsigned int)s_ntp_retry);
                DRV_UART_WriteBlock(WIFI_COM, s_ntp_req, NTP_MMI_PACKET_SIZE);
            } else {
                /* 重试耗尽，关闭链路收尾，时间有效性用NTP_MMI_TimeValid查询 */
                osal_timer_stop(s_wifitmr);
                WIFI_SendCmdExec(WIFI_CMD_CIPCLOSE, WifiCloseDone);
            }
        }
        break;

    case STEP_WIFI_LOST:
        /* 掉线延时到点：运行中掉线重连路由器，初始化中掉线复位重来 */
        osal_timer_stop(s_wifitmr);
        if (s_ready) {
            WifiStaSet(WIFI_STA_CONNECTING);
            WIFI_SendCmdExec(WIFI_CMD_CWJAP, WifiReconnDone);
        } else {
            WifiRestart();
        }
        break;

    case STEP_END:
    default:
        osal_timer_stop(s_wifitmr);
        break;
    }
}

/*******************************************************************
** 函数名	: WIFI_MMI_Init
** 函数描述	: WiFi模块初始化：复位脚配置、发送/接收模块初始化，
**			注册IPD回调并启动状态机，须在串口与定时器初始化后调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_MMI_Init(void)
{
    WifiGpioInit();

    WIFI_SendInit();
    WIFI_RecvInit();
    WIFI_RecvSetIpdCb(0, WifiIpdHandler);   /* 链路0：NTP UDP */

    s_step      = STEP_RESET_LOW;
    s_cur       = 0;
    s_ntp_retry = 0;
    s_ready     = 0;
    WifiStaSet(WIFI_STA_INIT);

    s_wifitmr = osal_timer_create((void *)0, WifiTmrProc);
    if (s_wifitmr != 0xff) {
        osal_timer_start(s_wifitmr, 1, 1);   /* 1ms后进入第一步 */
    }
}

/*******************************************************************
** 函数名	: WIFI_MMI_IsReady
** 函数描述	: 查询初始化流程是否完成(基础指令+NTP校时收尾)
** 参数		: 无
** 返回		: 1完成，0未完成
********************************************************************/
uint8_t WIFI_MMI_IsReady(void)
{
    return s_ready;
}

/*******************************************************************
** 函数名	: WIFI_MMI_GetSta
** 函数描述	: 查询STA连接状态
** 参数		: 无
** 返回		: WIFI_STA_E状态
********************************************************************/
WIFI_STA_E WIFI_MMI_GetSta(void)
{
    return s_sta;
}

/*******************************************************************
** 函数名	: WIFI_MMI_OnStaEvent
** 函数描述	: STA连接事件上报(接收侧"WIFI CONNECTED/DISCONNECT"行触发)：
**			记录状态，已连接后掉线自动重启重连/复位流程
** 参数		: [in] connected: 1已连接, 0断开
** 返回		: 无
********************************************************************/
void WIFI_MMI_OnStaEvent(uint8_t connected)
{
    if (connected) {
        WifiStaSet(WIFI_STA_CONNECTED);
        return;
    }

    /* 断开事件：仅已连接状态下才触发恢复，连接中/初始化中的DISCONNECT忽略 */
    if (s_sta == WIFI_STA_CONNECTED) {
        WifiStaSet(WIFI_STA_LOST);
        s_step = STEP_WIFI_LOST;
        osal_timer_start(s_wifitmr, 2000, 1);
    }
}

/*******************************************************************
** 函数名	: WIFI_MMI_Reset
** 函数描述	: 外部触发模块整体复位初始化(open连续失败自愈等场景)：
**			拉复位脚+重跑AT指令序列恢复CIPMUX/路由器连接
** 参数		: 无
** 返回		: 无
********************************************************************/
void WIFI_MMI_Reset(void)
{
    WifiRestart();
}
