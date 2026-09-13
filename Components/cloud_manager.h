/********************************************************************************
**
** 文件名:     cloud_manager.h
** 版权所有:   无
** 文件描述:   该模块主要实现云平台连接管理接口定义
**
*********************************************************************************/


#ifndef __CLOUD_MANAGER_H
#define __CLOUD_MANAGER_H

#include <stdint.h>

/* 云平台连接状态 */
typedef enum {
    CLOUD_STATE_DISCONNECTED = 0,
    CLOUD_STATE_CONNECTING,
    CLOUD_STATE_CONNECTED,
    CLOUD_STATE_ERROR
} CloudState_e;

/* 云平台消息类型 */
typedef enum {
    MSG_TYPE_SENSOR_DATA = 0,
    MSG_TYPE_DEVICE_STATUS,
    MSG_TYPE_OTA_COMMAND,
    MSG_TYPE_CONFIG_UPDATE,
    MSG_TYPE_MAX
} CloudMsgType_e;

/* 云平台消息结构 */
typedef struct {
    CloudMsgType_e type;
    uint8_t *payload;
    uint16_t payload_len;
    uint32_t timestamp;
} CloudMessage_t;

/* 云平台管理器接口 */
int cloud_manager_init(void);
int cloud_manager_deinit(void);
int cloud_manager_connect(void);
int cloud_manager_disconnect(void);
int cloud_manager_send_message(CloudMessage_t *msg);
int cloud_manager_recv_message(CloudMessage_t *msg, uint32_t timeout);
CloudState_e cloud_manager_get_state(void);
int cloud_manager_set_reconnect_interval(uint32_t interval);

#endif /* __CLOUD_MANAGER_H */
