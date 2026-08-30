#include <stdio.h>
#include "drv_uart.h"
#include "os_include.h"
#include "ntp_mmi.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_send.h"
#include "esp_wifi_recv.h"

/*
 * 流程编排参考yx_mmi_power.c的定时器状态机框架：
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
    STEP_END
} WIFI_STEP_E;

static INT8U s_wifitmr;
static WIFI_STEP_E s_step;
static uint8_t s_cur;           /* 当前指令索引 */
static uint8_t s_ntp_retry;     /* NTP请求已重发次数 */
static INT16U s_ntp_poll;       /* NTP应答轮询计数 */
static uint8_t s_ready;
static uint8_t s_ntp_req[NTP_MMI_PACKET_SIZE];

/* 前置声明 */
static void WifiSendCur(void);
static void WifiCipsendDone(uint8_t result);

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
** 函数名	: WifiIpdHandler
** 函数描述	: +IPD负载处理回调：打印原始数据，解析NTP应答建立时间基准，
**			成功后关闭UDP链路
** 参数		: [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
static void WifiIpdHandler(const uint8_t *data, uint16_t len)
{
    uint32_t unix_ts;
    uint16_t i;

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
        OS_StopTmr(s_wifitmr);
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
        s_cur++;
        if (s_cur < WIFI_SEQ_NUM) {
            if (s_init_seq[s_cur].gap_ms > 0) {
                /* 延时后发送下一条 */
                s_step = STEP_DELAY;
                OS_StartTmr(s_wifitmr, _MILTICK, s_init_seq[s_cur].gap_ms);
            } else {
                WifiSendCur();
            }
        } else {
            /* 基础指令完成，发起CIPSEND等'>'提示 */
            WIFI_SendCmdExec(WIFI_CMD_CIPSEND, WifiCipsendDone);
        }
    } else {
        printf("wifi init fail: cmd %u\r\n", (unsigned int)s_cur);
        s_step = STEP_END;
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
    OS_StartTmr(s_wifitmr, _MILTICK, 10);
}

/*******************************************************************
** 函数名	: WifiSendCur
** 函数描述	: 发送AT指令表当前指令
** 参数		: 无
** 返回		: 无
********************************************************************/
static void WifiSendCur(void)
{
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
        OS_StartTmr(s_wifitmr, _MILTICK, 200);
        break;

    case STEP_RESET_HIGH:
        /* 拉高复位脚，等模块重启完成 */
        HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
        s_step = STEP_SEND_AT;
        OS_StartTmr(s_wifitmr, _SECOND, 2);
        break;

    case STEP_SEND_AT:
        /* 开始发送AT指令表第一条 */
        OS_StopTmr(s_wifitmr);
        s_cur = 0;
        WifiSendCur();
        break;

    case STEP_DELAY:
        OS_StopTmr(s_wifitmr);
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
                OS_StopTmr(s_wifitmr);
                WIFI_SendCmdExec(WIFI_CMD_CIPCLOSE, WifiCloseDone);
            }
        }
        break;

    case STEP_END:
    default:
        OS_StopTmr(s_wifitmr);
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
    WIFI_RecvSetIpdCb(WifiIpdHandler);

    s_step      = STEP_RESET_LOW;
    s_cur       = 0;
    s_ntp_retry = 0;
    s_ready     = 0;

    s_wifitmr = OS_CreateTmr((void *)0, WifiTmrProc);
    if (s_wifitmr != 0xff) {
        OS_StartTmr(s_wifitmr, _MILTICK, 1);   /* 1ms后进入第一步 */
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
