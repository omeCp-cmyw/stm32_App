#ifndef __CLOUD_ONENET_CONFIG_H
#define __CLOUD_ONENET_CONFIG_H

/*
 * OneNET MQTT接入协议/时序配置：
 * 接入/鉴权参数(host/port/client_id/product_id等)已外置为
 * cloud_init(cfg)传入，本文件仅保留OneNET平台协议常量与
 * 接入时序/在线周期参数。
 */

/* MQTT keepalive */
#define ONENET_KEEPALIVE_SEC    60

/* 设备鉴权token参数(token由onenet_token_build动态生成) */
#define ONENET_TOKEN_VALID_SEC  2592000     /* token有效期30天, et=当前时间+有效期 */
#define ONENET_TOKEN_ET_FALLBACK 1872999428 /* 未校时的固定过期时间戳(2029年) */
#define ONENET_TOKEN_MAX_LEN    200

/* topic模板, 带%s处由产品ID/设备名填充 */
#define ONENET_TOPIC_POST          "$sys/%s/%s/thing/property/post"
#define ONENET_TOPIC_POST_REPLY    "$sys/%s/%s/thing/property/post/reply"
#define ONENET_TOPIC_EVENT_POST    "$sys/%s/%s/thing/event/post"
#define ONENET_TOPIC_EVENT_REPLY   "$sys/%s/%s/thing/event/post/reply"
#define ONENET_TOPIC_SET           "$sys/%s/%s/thing/property/set"
#define ONENET_TOPIC_SET_REPLY     "$sys/%s/%s/thing/property/set_reply"
#define ONENET_TOPIC_DESIRED_GET   "$sys/%s/%s/thing/property/desired/get"
#define ONENET_TOPIC_DESIRED_REPLY "$sys/%s/%s/thing/property/desired/get/reply"
#define ONENET_TOPIC_MAX_LEN       128

/* 应答超时(ms) */
#define ONENET_CONNACK_TIMEOUT_MS 10000
#define ONENET_SUBACK_TIMEOUT_MS  10000
#define ONENET_PINGRESP_TIMEOUT_MS 10000

/* 接入时序: CONNACK后稍等再订阅, 上线后延迟首报 */
#define ONENET_SUB_DELAY_MS         2000
#define ONENET_FIRST_REPORT_DELAY_MS 5000

/* 在线周期(ms) */
#define ONENET_REPORT_MS        10000   /* 属性上报周期 */
#define ONENET_HEARTBEAT_MS     20000   /* PINGREQ周期: 平台要求keepalive(60s)一半内
                                         * 必须发PINGREQ, 30s是判定边界无余量;
                                         * 20s留出安全余量且避开与10s上报的整数倍
                                         * 撞车(撞车时busy排队补发机制兜底) */

#endif /* __CLOUD_ONENET_CONFIG_H */
