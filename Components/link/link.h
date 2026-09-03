#ifndef LINK_H
#define LINK_H

#include <stdint.h>

/*
 * 链路抽象层：TCP客户端统一接口，解耦上层组件(cloud/ntp/ota)与联网模组。
 * 上层一律通过link_get()收发，不接触具体模组的AT指令/链路号/引脚。
 * 换模组(ESP8266->ESP32/4G/以太网)只需新增link_xxx.c实现本接口。
 */

/* 链路事件 */
typedef enum {
    LINK_EV_CONNECTED,      /* 连接建立(data=NULL, len=0) */
    LINK_EV_DATA,           /* 收到数据(data/len有效) */
    LINK_EV_CLOSED,         /* 链路关闭: 被动断链/WiFi掉线(data=NULL) */
    LINK_EV_OPEN_FAIL,      /* 连接失败: 拒绝/超时(data=NULL) */
} link_event_t;

/* 链路事件回调(open时注册, 连接期持续有效) */
typedef void (*link_event_cb_t)(link_event_t ev, const uint8_t *data, int len);

/* 发送完成回调: ok=1发送成功, 0失败 */
typedef void (*link_send_done_t)(int ok);

/* 链路接口 */
typedef struct {
    int     (*open)(const char *host, uint16_t port, link_event_cb_t ev_cb);
    int     (*send)(const uint8_t *data, int len, link_send_done_t done);
    void    (*close)(void);
    uint8_t (*is_ready)(void);
    uint32_t (*get_discnt)(void);   /* 断链累计次数(上电起, 被动断链/WiFi掉线) */
} link_if_t;

/*******************************************************************
** 函数名	: link_get
** 函数描述	: 获取当前板级配置选定的链路实现
** 参数		: 无
** 返回		: link_if_t接口指针
********************************************************************/
const link_if_t *link_get(void);

/*******************************************************************
** 函数名	: link_init
** 函数描述	: 链路层初始化(含模组AT引擎初始化与自身定时器)，
**			须在osal_init之后调用
** 参数		: 无
** 返回		: 无
********************************************************************/
void link_init(void);

#endif /* LINK_H */
