#include <stdio.h>
#include <string.h>
#include "osal.h"
#include "link.h"
#include "esp_wifi_mmi.h"
#include "esp_wifi_send.h"
#include "esp_wifi_recv.h"

/* 外部OTA链路初始化函数 */
extern void link_ota_init(void);

/*
 * link_if_t的ESP8266 AT实现：
 * 复用esp_wifi的AT事务引擎(发送/接收/重试)，对外收敛为TCP客户端语义。
 * 链路分配：链路0=NTP校时(esp_wifi_mmi初始化流程内)，链路1=上层TCP。
 * 上层组件(cloud/ntp/ota)不感知链路号与AT细节，只通过link_if_t收发。
 */

#define ESP_LINK_TCP            1       /* 上层TCP固定走链路1 */

#define ESP_OPEN_TIMEOUT_MS     10000   /* CIPSTART应答超时 */
#define ESP_OPEN_RETRIES        1       /* 额外重试次数(总发送=2) */
#define ESP_CLOSE_TIMEOUT_MS    5000    /* CIPCLOSE应答超时 */
#define ESP_OPEN_FAIL_RESET     3       /* open连续失败次数达此值复位模块自愈 */

#define ESP_POLL_MS             100     /* STA掉线检测轮询周期 */

static link_event_cb_t s_ev_cb;         /* 上层事件回调(open时注册) */
static link_send_done_t s_done;         /* 当前发送事务完成回调 */
static INT8U s_tmr;                     /* STA掉线检测定时器 */
static uint8_t s_open;                  /* 链路已建立 */
static uint8_t s_busy;                  /* 发送事务进行中 */
static uint8_t s_open_fail;             /* open连续失败计数(达阈值复位模块) */
static uint32_t s_discnt;               /* 断链累计次数(上电起) */
static char s_cmd[80];                  /* AT指令组装缓冲 */

/* 前置声明 */
static int  LinkEspOpen(const char *host, uint16_t port, link_event_cb_t ev_cb);
static int  LinkEspSend(const uint8_t *data, int len, link_send_done_t done);
static void LinkEspClose(void);
static uint8_t LinkEspIsReady(void);
static uint32_t LinkEspGetDiscnt(void);

static const link_if_t s_link_if = {
    LinkEspOpen,
    LinkEspSend,
    LinkEspClose,
    LinkEspIsReady,
    LinkEspGetDiscnt,
};

/*******************************************************************
** 函数名	: LinkEspIpdCb
** 函数描述	: 链路1的+IPD负载回调：转发给上层事件回调
** 参数		: [in] link: 链路号
**          : [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
static void LinkEspIpdCb(uint8_t link, const uint8_t *data, uint16_t len)
{
    if (link != ESP_LINK_TCP || s_ev_cb == 0) {
        return;
    }
    s_ev_cb(LINK_EV_DATA, data, len);
}

/*******************************************************************
** 函数名	: LinkEspCloseCb
** 函数描述	: 链路1关闭事件回调：复位打开标志并上报断链事件
** 参数		: [in] link: 链路号
** 返回		: 无
********************************************************************/
static void LinkEspCloseCb(uint8_t link)
{
    if (link != ESP_LINK_TCP) {
        return;
    }
    /* 链路打开期间的断链才计数：WiFi掉线后模块补报的CLOSED不重复计 */
    if (s_open) {
        s_discnt++;
    }
    s_open = 0;
    s_busy = 0;
    if (s_ev_cb != 0) {
        s_ev_cb(LINK_EV_CLOSED, 0, 0);
    }
}

/*******************************************************************
** 函数名	: LinkEspOpenDone
** 函数描述	: CIPSTART发送结果回调：成功/失败映射为连接事件
** 参数		: [in] result: 发送结果(WIFI_SEND_*)
** 返回		: 无
********************************************************************/
static void LinkEspOpenDone(uint8_t result)
{
    if (s_ev_cb == 0) {
        return;
    }
    if (result == WIFI_SEND_OK) {
        s_open_fail = 0;
        s_open = 1;
        s_ev_cb(LINK_EV_CONNECTED, 0, 0);
    } else {
        /* 超时(模块完全无应答)=死机铁证, 立即复位模块自愈;
         * 其余失败(收到ERROR应答, 模块存活)连续N次才复位,
         * 覆盖CIPMUX状态漂移("Link typ ERROR")场景 */
        if (result == WIFI_SEND_OVERTIME ||
            ++s_open_fail >= ESP_OPEN_FAIL_RESET) {
            s_open_fail = 0;
            printf("[link] open fail, reset wifi module\r\n");
            WIFI_MMI_Reset();
        }
        s_ev_cb(LINK_EV_OPEN_FAIL, 0, 0);
    }
}

/*******************************************************************
** 函数名	: LinkEspSendDone
** 函数描述	: 链路数据发送结果回调：通知上层发送完成
** 参数		: [in] result: 发送结果(WIFI_SEND_*)
** 返回		: 无
********************************************************************/
static void LinkEspSendDone(uint8_t result)
{
    link_send_done_t done = s_done;

    s_busy = 0;
    s_done = 0;
    /* 发送超时(模块完全无应答)=死机铁证, 立即复位模块自愈 */
    if (result == WIFI_SEND_OVERTIME) {
        s_open_fail = 0;
        printf("[link] send overtime, reset wifi module\r\n");
        WIFI_MMI_Reset();
    }
    if (done != 0) {
        done(result == WIFI_SEND_OK ? 1 : 0);
    }
}

/*******************************************************************
** 函数名	: LinkEspTmrProc
** 函数描述	: 周期轮询：STA掉线时链路随模块断开，主动上报断链事件，
**			避免上层依赖ping超时才感知
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void LinkEspTmrProc(void *pdata)
{
    pdata = pdata;

    if (s_open && WIFI_MMI_GetSta() != WIFI_STA_CONNECTED) {
        s_open = 0;
        s_busy = 0;
        s_discnt++;
        printf("[link] wifi lost, link down\r\n");
        if (s_ev_cb != 0) {
            s_ev_cb(LINK_EV_CLOSED, 0, 0);
        }
    }
}

/*******************************************************************
** 函数名	: LinkEspOpen
** 函数描述	: 建立链路1的TCP连接(CIPSTART)，结果经事件回调通知
** 参数		: [in] host: 服务器域名/IP
**          : [in] port: 端口
**          : [in] ev_cb: 链路事件回调
** 返回		: 1已发起(异步), 0链路未就绪
********************************************************************/
static int LinkEspOpen(const char *host, uint16_t port, link_event_cb_t ev_cb)
{
    if (!LinkEspIsReady() || s_open) {
        return 0;
    }

    s_ev_cb = ev_cb;
    snprintf(s_cmd, sizeof(s_cmd), "AT+CIPSTART=%u,\"TCP\",\"%s\",%u\r\n",
             (unsigned int)ESP_LINK_TCP, host, (unsigned int)port);

    if (!WIFI_SendListSend("OK", "ERROR", s_cmd, (uint16_t)strlen(s_cmd),
                           ESP_OPEN_RETRIES + 1, ESP_OPEN_TIMEOUT_MS,
                           LinkEspOpenDone)) {
        return 0;
    }
    return 1;
}

/*******************************************************************
** 函数名	: LinkEspSend
** 函数描述	: 链路1二进制数据发送(CIPSEND)，结果回调通知
** 参数		: [in] data: 数据(调用方保持有效至回调)
**          : [in] len: 数据长度
**          : [in] done: 发送完成回调
** 返回		: 1入队成功, 0链路未建立或事务占用
********************************************************************/
static int LinkEspSend(const uint8_t *data, int len, link_send_done_t done)
{
    if (!s_open || s_busy) {
        return 0;
    }
    if (!WIFI_SendData(ESP_LINK_TCP, data, (uint16_t)len, LinkEspSendDone)) {
        return 0;
    }
    s_busy = 1;
    s_done = done;
    return 1;
}

/*******************************************************************
** 函数名	: LinkEspClose
** 函数描述	: 主动关闭链路1(CIPCLOSE)，结果不回调
** 参数		: 无
** 返回		: 无
********************************************************************/
static void LinkEspClose(void)
{
    if (!s_open) {
        return;
    }
    s_open = 0;

    snprintf(s_cmd, sizeof(s_cmd), "AT+CIPCLOSE=%u\r\n",
             (unsigned int)ESP_LINK_TCP);
    WIFI_SendListSend("OK", 0, s_cmd, (uint16_t)strlen(s_cmd),
                      1, ESP_CLOSE_TIMEOUT_MS, 0);
}

/*******************************************************************
** 函数名	: LinkEspIsReady
** 函数描述	: 查询链路是否可发起连接(模块初始化完成且路由器已连接)
** 参数		: 无
** 返回		: 1就绪, 0未就绪
********************************************************************/
static uint8_t LinkEspIsReady(void)
{
    return WIFI_MMI_IsReady() && WIFI_MMI_GetSta() == WIFI_STA_CONNECTED;
}

/*******************************************************************
** 函数名	: LinkEspGetDiscnt
** 函数描述	: 查询断链累计次数
** 参数		: 无
** 返回		: 断链累计次数(上电起)
********************************************************************/
static uint32_t LinkEspGetDiscnt(void)
{
    return s_discnt;
}

/*******************************************************************
** 函数名	: link_get
** 函数描述	: 获取链路接口实例
** 参数		: 无
** 返回		: link_if_t接口指针
********************************************************************/
const link_if_t *link_get(void)
{
    return &s_link_if;
}

/*******************************************************************
** 函数名	: link_init
** 函数描述	: 链路层初始化：WiFi AT引擎初始化、链路1回调注册、
**			STA掉线检测定时器，须在osal_init之后调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void link_init(void)
{
    /* WiFi模块AT引擎：PG15复位+AT指令序列+NTP校时(链路0) */
    WIFI_MMI_Init();

    /* 链路1: 上层TCP数据与断链事件，统一由link层接管 */
    WIFI_RecvSetIpdCb(ESP_LINK_TCP, LinkEspIpdCb);
    WIFI_RecvSetCloseCb(ESP_LINK_TCP, LinkEspCloseCb);

    s_open = 0;
    s_busy = 0;
    s_open_fail = 0;
    s_discnt = 0;
    s_ev_cb = 0;
    s_done = 0;

    s_tmr = osal_timer_create((void *)0, LinkEspTmrProc);
    if (s_tmr != 0xff) {
        osal_timer_start(s_tmr, ESP_POLL_MS, 1);
    }
    
    /* 链路2: OTA HTTP数据与断链事件 */
    link_ota_init();
}
