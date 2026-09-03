#ifndef __MQTT_MMI_H
#define __MQTT_MMI_H

#include <stdint.h>

/*
 * MQTT 3.1.1协议编解码与帧重组（移植自wifi_pro的proto/mqtt.c）：
 * CONNECT/SUBSCRIBE/PUBLISH/PINGREQ组帧，
 * +IPD负载可能跨包分片，帧重组缓冲凑整帧后解析
 */

/* 报文类型(高4位) */
#define MQTT_PKT_CONNECT     0x10
#define MQTT_PKT_CONNACK     0x20
#define MQTT_PKT_PUBLISH     0x30
#define MQTT_PKT_PUBACK      0x40
#define MQTT_PKT_SUBSCRIBE   0x82
#define MQTT_PKT_SUBACK      0x90
#define MQTT_PKT_PINGREQ     0xC0
#define MQTT_PKT_PINGRESP    0xD0
#define MQTT_PKT_DISCONNECT  0xE0

#define MQTT_REASM_SIZE      1024

/* 解析出的一帧 */
typedef struct {
    uint8_t type;               /* 首字节(含低4位标志) */
    int remlen;                 /* 剩余长度 */
    const uint8_t *body;        /* 剩余部分 */
} mqtt_frame_t;

/* PUBLISH帧解析字段 */
typedef struct {
    int qos;
    const char *topic;          /* 临时缓冲中以'\0'结尾 */
    const uint8_t *payload;
    int payload_len;
} mqtt_pub_t;

/*******************************************************************
** 函数名	: mqtt_build_connect
** 函数描述	: 组CONNECT报文
** 参数		: [out] buf: 输出缓冲
**          : [in] buflen: 缓冲大小
**          : [in] client_id: 客户端标识
**          : [in] user: 用户名
**          : [in] pass: 密码(token)
**          : [in] keepalive: 心跳周期(秒)
** 返回		: 报文长度, -1缓冲不足
********************************************************************/
int mqtt_build_connect(uint8_t *buf, int buflen, const char *client_id,
                       const char *user, const char *pass, int keepalive);

/*******************************************************************
** 函数名	: mqtt_build_subscribe
** 函数描述	: 组SUBSCRIBE报文(单topic, 报文标识自动递增)
** 参数		: [out] buf: 输出缓冲
**          : [in] buflen: 缓冲大小
**          : [in] topic: 主题
**          : [in] qos: 订阅质量
** 返回		: 报文长度, -1缓冲不足
********************************************************************/
int mqtt_build_subscribe(uint8_t *buf, int buflen, const char *topic, int qos);

/*******************************************************************
** 函数名	: mqtt_build_publish
** 函数描述	: 组QoS0 PUBLISH报文
** 参数		: [out] buf: 输出缓冲
**          : [in] buflen: 缓冲大小
**          : [in] topic: 主题
**          : [in] payload: 消息负载
**          : [in] plen: 负载长度
** 返回		: 报文长度, -1缓冲不足
********************************************************************/
int mqtt_build_publish(uint8_t *buf, int buflen, const char *topic,
                       const uint8_t *payload, int plen);

/*******************************************************************
** 函数名	: mqtt_build_pingreq
** 函数描述	: 组PINGREQ报文
** 参数		: [out] buf: 输出缓冲(至少2字节)
** 返回		: 报文长度(2)
********************************************************************/
int mqtt_build_pingreq(uint8_t *buf);

/*******************************************************************
** 函数名	: mqtt_reasm_feed
** 函数描述	: 链路负载喂入重组缓冲(可能跨包分片)
** 参数		: [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
void mqtt_reasm_feed(const uint8_t *data, int len);

/*******************************************************************
** 函数名	: mqtt_reasm_next
** 函数描述	: 从重组缓冲查看下一整帧(不移除, body指针仅consume前有效)
** 参数		: [out] f: 帧结构
** 返回		: 0取出一帧, -1数据不完整
********************************************************************/
int mqtt_reasm_next(mqtt_frame_t *f);

/*******************************************************************
** 函数名	: mqtt_reasm_consume
** 函数描述	: 移除mqtt_reasm_next刚返回的一帧, 剩余前移
** 参数		: 无
** 返回		: 无
********************************************************************/
void mqtt_reasm_consume(void);

/*******************************************************************
** 函数名	: mqtt_parse_publish
** 函数描述	: 解析PUBLISH帧的topic与负载
** 参数		: [in] f: 帧结构
**          : [out] pub: 字段结构
** 返回		: 0成功, -1格式非法
********************************************************************/
int mqtt_parse_publish(const mqtt_frame_t *f, mqtt_pub_t *pub);

/*******************************************************************
** 函数名	: mqtt_type_name
** 函数描述	: 报文类型转可读名(供打印用)
** 参数		: [in] type: 首字节
** 返回		: 类型名字符串
********************************************************************/
const char *mqtt_type_name(uint8_t type);

#endif /* __MQTT_MMI_H */
