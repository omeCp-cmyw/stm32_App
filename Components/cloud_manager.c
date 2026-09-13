/********************************************************************************
**
** 文件名:     cloud_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现云平台连接管理
**
*********************************************************************************/


#include "cloud_manager.h"
#include <stdio.h>
#include <string.h>

/* 云平台状态（桩函数） */
static CloudState_e cloud_state = CLOUD_STATE_DISCONNECTED;

/* 重连间隔（桩函数） */
static uint32_t reconnect_interval = 5000;

/*******************************************************************************
** 函数名称    cloud_manager_init
** 函数说明    初始化云平台管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_init(void)
{
    (void)reconnect_interval;  /* 消除未使用变量警告，未来实现时使用 */
    cloud_state = CLOUD_STATE_DISCONNECTED;
    
    printf("[CLOUD_MANAGER] Cloud manager initialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_deinit
** 函数说明    反初始化云平台管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_deinit(void)
{
    cloud_state = CLOUD_STATE_DISCONNECTED;
    
    printf("[CLOUD_MANAGER] Cloud manager deinitialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_connect
** 函数说明    连接云平台（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_connect(void)
{
    cloud_state = CLOUD_STATE_CONNECTING;
    
    printf("[CLOUD_MANAGER] Connecting to cloud (stub)\r\n");
    
    /* 桩函数，直接设置为已连接 */
    cloud_state = CLOUD_STATE_CONNECTED;
    
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_disconnect
** 函数说明    断开云平台连接（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_disconnect(void)
{
    cloud_state = CLOUD_STATE_DISCONNECTED;
    
    printf("[CLOUD_MANAGER] Disconnected from cloud (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_send_message
** 函数说明    发送消息到云平台（桩函数）
** 输入参数    msg: 消息指针
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_send_message(CloudMessage_t *msg)
{
    if (msg == NULL) {
        return -1;
    }
    
    if (cloud_state != CLOUD_STATE_CONNECTED) {
        printf("[CLOUD_MANAGER] Not connected to cloud\r\n");
        return -1;
    }
    
    printf("[CLOUD_MANAGER] Send message type %d (stub)\r\n", msg->type);
    return 0;
}

/*******************************************************************************
** 函数名称    cloud_manager_recv_message
** 函数说明    接收云平台消息（桩函数）
** 输入参数    msg: 消息指针
**             timeout: 超时时间(ms)
** 输出参数    无
** 返回参数    0: 成功, -1: 失败, -2: 超时
*******************************************************************************/
int cloud_manager_recv_message(CloudMessage_t *msg, uint32_t timeout)
{
    if (msg == NULL) {
        return -1;
    }
    
    if (cloud_state != CLOUD_STATE_CONNECTED) {
        printf("[CLOUD_MANAGER] Not connected to cloud\r\n");
        return -1;
    }
    
    /* 桩函数，返回超时 */
    printf("[CLOUD_MANAGER] Receive message timeout (stub)\r\n");
    return -2;
}

/*******************************************************************************
** 函数名称    cloud_manager_get_state
** 函数说明    获取云平台连接状态（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    连接状态
*******************************************************************************/
CloudState_e cloud_manager_get_state(void)
{
    return cloud_state;
}

/*******************************************************************************
** 函数名称    cloud_manager_set_reconnect_interval
** 函数说明    设置重连间隔（桩函数）
** 输入参数    interval: 重连间隔(ms)
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int cloud_manager_set_reconnect_interval(uint32_t interval)
{
    reconnect_interval = interval;
    
    printf("[CLOUD_MANAGER] Set reconnect interval to %d ms (stub)\r\n", (int)interval);
    return 0;
}
