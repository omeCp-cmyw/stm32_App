/********************************************************************************
**
** 文件名:     net_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现网络连接管理
**
*********************************************************************************/


#include "net_manager.h"
#include <stdio.h>
#include <string.h>

/* 链路信息表（桩函数） */
static NetLinkInfo_t link_info[LINK_MAX];

/*******************************************************************************
** 函数名称    net_manager_init
** 函数说明    初始化网络管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int net_manager_init(void)
{
    memset(link_info, 0, sizeof(link_info));
    
    printf("[NET_MANAGER] Net manager initialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    net_manager_deinit
** 函数说明    反初始化网络管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int net_manager_deinit(void)
{
    printf("[NET_MANAGER] Net manager deinitialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    net_manager_create_link
** 函数说明    创建网络链路（桩函数）
** 输入参数    id: 链路ID
**             priority: 优先级
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int net_manager_create_link(NetLinkID_e id, uint8_t priority)
{
    if (id >= LINK_MAX) {
        return -1;
    }
    
    link_info[id].id = id;
    link_info[id].state = LINK_STATE_CONNECTING;
    link_info[id].priority = priority;
    link_info[id].socket = -1;
    
    printf("[NET_MANAGER] Create link %d with priority %d (stub)\r\n", id, priority);
    
    /* 桩函数，直接设置为已连接 */
    link_info[id].state = LINK_STATE_CONNECTED;
    
    return 0;
}

/*******************************************************************************
** 函数名称    net_manager_destroy_link
** 函数说明    销毁网络链路（桩函数）
** 输入参数    id: 链路ID
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int net_manager_destroy_link(NetLinkID_e id)
{
    if (id >= LINK_MAX) {
        return -1;
    }
    
    link_info[id].state = LINK_STATE_IDLE;
    link_info[id].socket = -1;
    
    printf("[NET_MANAGER] Destroy link %d (stub)\r\n", id);
    return 0;
}

/*******************************************************************************
** 函数名称    net_manager_send
** 函数说明    发送数据（桩函数）
** 输入参数    id: 链路ID
**             data: 数据指针
**             len: 数据长度
** 输出参数    无
** 返回参数    发送的字节数, -1: 失败
*******************************************************************************/
int net_manager_send(NetLinkID_e id, const uint8_t *data, uint16_t len)
{
    if (id >= LINK_MAX || data == NULL) {
        return -1;
    }
    
    if (link_info[id].state != LINK_STATE_CONNECTED) {
        printf("[NET_MANAGER] Link %d not connected\r\n", id);
        return -1;
    }
    
    printf("[NET_MANAGER] Send %d bytes on link %d (stub)\r\n", len, id);
    return len;
}

/*******************************************************************************
** 函数名称    net_manager_recv
** 函数说明    接收数据（桩函数）
** 输入参数    id: 链路ID
**             buf: 缓冲区指针
**             len: 缓冲区长度
**             timeout: 超时时间(ms)
** 输出参数    无
** 返回参数    接收的字节数, -1: 失败, -2: 超时
*******************************************************************************/
int net_manager_recv(NetLinkID_e id, uint8_t *buf, uint16_t len, uint32_t timeout)
{
    if (id >= LINK_MAX || buf == NULL) {
        return -1;
    }
    
    if (link_info[id].state != LINK_STATE_CONNECTED) {
        printf("[NET_MANAGER] Link %d not connected\r\n", id);
        return -1;
    }
    
    /* 桩函数，返回超时 */
    printf("[NET_MANAGER] Receive timeout on link %d (stub)\r\n", id);
    return -2;
}

/*******************************************************************************
** 函数名称    net_manager_get_link_state
** 函数说明    获取链路状态（桩函数）
** 输入参数    id: 链路ID
** 输出参数    无
** 返回参数    链路状态
*******************************************************************************/
NetLinkState_e net_manager_get_link_state(NetLinkID_e id)
{
    if (id >= LINK_MAX) {
        return LINK_STATE_ERROR;
    }
    
    return link_info[id].state;
}

/*******************************************************************************
** 函数名称    net_manager_get_link_info
** 函数说明    获取链路信息（桩函数）
** 输入参数    id: 链路ID
**             info: 信息指针
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int net_manager_get_link_info(NetLinkID_e id, NetLinkInfo_t *info)
{
    if (id >= LINK_MAX || info == NULL) {
        return -1;
    }
    
    *info = link_info[id];
    
    printf("[NET_MANAGER] Get link %d info (stub)\r\n", id);
    return 0;
}
