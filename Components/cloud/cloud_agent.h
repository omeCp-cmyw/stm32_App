#ifndef CLOUD_AGENT_H
#define CLOUD_AGENT_H

#include <stdint.h>

/*
 * 云平台抽象：统一接口，插拔式。
 * 应用层(app_gateway)只依赖本头文件，接入OneNET/阿里云/腾讯云
 * 只需新增cloud_xxx.c实现本接口，应用层零改动。
 * 传输层依赖链路抽象link_if_t，与具体联网模组解耦。
 */

/* 云平台接入状态 */
typedef enum {
    CLOUD_ST_OFFLINE = 0,   /* 未接入/已断链 */
    CLOUD_ST_TCP,           /* TCP连接建立中 */
    CLOUD_ST_CONNECT,       /* MQTT连接鉴权中 */
    CLOUD_ST_SUBSCRIBE,     /* 订阅中 */
    CLOUD_ST_ONLINE,        /* 在线 */
    CLOUD_ST_RECONN         /* 断链重连等待 */
} cloud_state_t;

/* 云端接入配置(由App层从gateway_config.h组装) */
typedef struct {
    const char *host;           /* MQTT broker域名/IP */
    uint16_t    port;           /* broker端口 */
    const char *client_id;      /* 客户端ID(产品下唯一) */
    const char *username;       /* 用户名(OneNET为产品ID) */
    const char *access_key;     /* 鉴权密钥(token由组件动态生成) */
    const char *product_id;     /* 产品ID(topic填充) */
    const char *device_name;    /* 设备名(topic填充) */
} cloud_cfg_t;

/* 云端属性下发回调: key=属性名, value=值文本 */
typedef void (*cloud_on_set_t)(const char *key, const char *value);

/*******************************************************************
** 函数名	: cloud_init
** 函数描述	: 云平台接入初始化并启动接入状态机，须在link_init之后调用
** 参数		: [in] cfg:    接入配置(内部拷贝, 调用方可释放)
**          : [in] on_set: 属性下发回调，可传0
** 返回		: 1成功, 0失败
********************************************************************/
int cloud_init(const cloud_cfg_t *cfg, cloud_on_set_t on_set);

/*******************************************************************
** 函数名	: cloud_report_property
** 函数描述	: 属性上报(JSON由上层物模型组装, 组件原样发布)
** 参数		: [in] json: 上报JSON文本
** 返回		: 1发送入队, 0失败
********************************************************************/
int cloud_report_property(const char *json);

/*******************************************************************
** 函数名	: cloud_post_event
** 函数描述	: 事件上报
** 参数		: [in] event_id: 事件ID
**          : [in] json: 事件参数JSON
** 返回		: 1发送入队, 0失败
********************************************************************/
int cloud_post_event(const char *event_id, const char *json);

/*******************************************************************
** 函数名	: cloud_set_property
** 函数描述	: 本地属性值更新(传感器采集写入物模型, 周期上报携带)
** 参数		: [in] key: 属性名(见product_def.h)
**          : [in] value: 值文本("25.5"/"45"/"true")
** 返回		: 1成功, 0未知属性
********************************************************************/
int cloud_set_property(const char *key, const char *value);

/*******************************************************************
** 函数名	: cloud_is_online
** 函数描述	: 查询云平台是否已接入在线
** 参数		: 无
** 返回		: 1在线, 0未接入
********************************************************************/
uint8_t cloud_is_online(void);

/*******************************************************************
** 函数名	: cloud_get_state
** 函数描述	: 查询云平台接入状态
** 参数		: 无
** 返回		: cloud_state_t状态
********************************************************************/
cloud_state_t cloud_get_state(void);

#endif /* CLOUD_AGENT_H */
