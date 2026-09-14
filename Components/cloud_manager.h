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

/*******************************************************************************
** 函数名称    cloud_manager_post_event
** 函数说明    上报物模型事件(信息型, 如LED开关事件)
** 输入参数    event_id: 事件标识符(如"led")
**             params_json: 事件参数JSON(如"{\"switch\":1}")
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int cloud_manager_post_event(const char *event_id, const char *params_json);

/*******************************************************************************
** 函数名称    cloud_manager_desired_get
** 函数说明    获取属性期望值(发布desired/get, 应答由mqtt线程解析应用)
** 输入参数    props_json: 属性名数组JSON(如"[\"led\"]")
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int cloud_manager_desired_get(const char *props_json);

/*******************************************************************************
** 函数名称    cloud_smoke_rule_update
** 函数说明    MQ2气体防抖规则更新: 1s快采样值输入, 连续N次超阈值才翻转告警
** 输入参数    smoke_mv: MQ2气体ADC电压(mV)
** 输出参数    无
** 返回参数    无
*******************************************************************************/
void cloud_smoke_rule_update(uint16_t smoke_mv);

#endif /* __CLOUD_MANAGER_H */
