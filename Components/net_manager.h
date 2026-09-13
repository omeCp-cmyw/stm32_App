/********************************************************************************
**
** 文件名:     net_manager.h
** 版权所有:   无
** 文件描述:   该模块主要实现网络连接管理接口定义
**
*********************************************************************************/


#ifndef __NET_MANAGER_H
#define __NET_MANAGER_H

#include <stdint.h>

/* 链路类型定义 */
typedef enum {
    LINK_MQTT = 0,
    LINK_HTTP,
    LINK_OTA,
    LINK_NTP,
    LINK_DEBUG,
    LINK_MAX
} NetLinkID_e;

/* 链路状态定义 */
typedef enum {
    LINK_STATE_IDLE = 0,
    LINK_STATE_CONNECTING,
    LINK_STATE_CONNECTED,
    LINK_STATE_DISCONNECTED,
    LINK_STATE_ERROR
} NetLinkState_e;

/* 链路信息结构 */
typedef struct {
    NetLinkID_e id;
    NetLinkState_e state;
    int socket;
    uint8_t priority;
    uint32_t timeout;
    uint32_t last_active_time;
} NetLinkInfo_t;

/* 网络管理器接口 */
int net_manager_init(void);
int net_manager_deinit(void);
int net_manager_create_link(NetLinkID_e id, uint8_t priority);
int net_manager_destroy_link(NetLinkID_e id);
int net_manager_send(NetLinkID_e id, const uint8_t *data, uint16_t len);
int net_manager_recv(NetLinkID_e id, uint8_t *buf, uint16_t len, uint32_t timeout);
NetLinkState_e net_manager_get_link_state(NetLinkID_e id);
int net_manager_get_link_info(NetLinkID_e id, NetLinkInfo_t *info);

#endif /* __NET_MANAGER_H */
