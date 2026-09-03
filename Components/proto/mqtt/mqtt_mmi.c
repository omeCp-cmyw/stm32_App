#include <stdio.h>
#include <string.h>
#include "mqtt_mmi.h"

/*
 * MQTT 3.1.1协议编解码与帧重组（移植自wifi_pro的proto/mqtt.c）：
 * 变长剩余长度编码，UTF-8字符串2字节长度前缀，
 * +IPD负载可能跨包分片，帧重组缓冲凑整帧后解析
 */

/* 重组缓冲: +IPD负载可能跨包分片, 帧重组凑整帧 */
static uint8_t s_reasm[MQTT_REASM_SIZE];
static int s_reasm_len;

/* SUBSCRIBE报文标识, QoS0的PUBLISH不用 */
static uint16_t s_pkt_id = 1;

/* PUBLISH解析时topic的临时缓冲 */
static char s_topic_tmp[160];

/* next返回帧总长, 供consume移除 */
static int s_frame_total;

/*******************************************************************
** 函数名	: remlen_encode
** 函数描述	: 编码MQTT变长剩余长度
** 参数		: [out] out: 输出位置
**          : [in] rem_len: 剩余长度值
** 返回		: 占用字节数, -1超长
********************************************************************/
static int remlen_encode(uint8_t *out, int rem_len)
{
    int used_bytes = 0;

    do {
        uint8_t digit = rem_len % 128;

        rem_len /= 128;
        if (rem_len > 0) {
            digit |= 0x80;
        }
        out[used_bytes++] = digit;
        if (used_bytes > 4) {
            return -1;
        }
    } while (rem_len > 0);
    return used_bytes;
}

/*******************************************************************
** 函数名	: remlen_decode
** 函数描述	: 解码变长剩余长度
** 参数		: [in] src: 数据位置
**          : [in] avail: 可用字节数
**          : [out] val: 解码值
**          : [out] used: 占用字节数
** 返回		: 0成功, -1数据不完整, -2格式非法
********************************************************************/
static int remlen_decode(const uint8_t *src, int avail, int *val, int *used)
{
    int multiplier = 1, decoded = 0, pos = 0;

    for (;;) {
        if (pos >= avail) {
            return -1;
        }
        decoded += (src[pos] & 0x7F) * multiplier;
        multiplier *= 128;
        if ((src[pos] & 0x80) == 0) {
            *val = decoded;
            *used = pos + 1;
            return 0;
        }
        pos++;
        if (pos >= 4) {
            return -2;
        }
    }
}

/*******************************************************************
** 函数名	: utf8_str_put
** 函数描述	: 写MQTT的2字节长度前缀字符串
** 参数		: [out] out: 输出位置
**          : [in] text: 字符串
**          : [in] text_len: 字符串长度
** 返回		: 写入字节数
********************************************************************/
static int utf8_str_put(uint8_t *out, const char *text, int text_len)
{
    out[0] = (uint8_t)(text_len >> 8);
    out[1] = (uint8_t)(text_len & 0xFF);
    memcpy(out + 2, text, text_len);
    return 2 + text_len;
}

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
                       const char *user, const char *pass, int keepalive)
{
    /* body静态化：512B大缓冲入栈会抬高CONNECT发送路径栈峰值，
     * 函数单线程调用(仅onenet接入流程)，不可重入可接受 */
    static uint8_t body[512];
    uint8_t rem_bytes[4];
    int pkt_len, rem_used, body_len = 0;
    int client_id_len = strlen(client_id);
    int user_len = strlen(user);
    int pass_len = strlen(pass);

    /* 可变头: 协议名+等级+连接标志+心跳 */
    body_len += utf8_str_put(body + body_len, "MQTT", 4);
    body[body_len++] = 4;                       /* 3.1.1 */
    body[body_len++] = 0xC2;                    /* 用户名+密码+清理会话 */
    body[body_len++] = (uint8_t)(keepalive >> 8);
    body[body_len++] = (uint8_t)(keepalive & 0xFF);

    body_len += utf8_str_put(body + body_len, client_id, client_id_len);
    body_len += utf8_str_put(body + body_len, user, user_len);
    body_len += utf8_str_put(body + body_len, pass, pass_len);

    rem_used = remlen_encode(rem_bytes, body_len);
    pkt_len = 1 + rem_used + body_len;
    if (pkt_len > buflen) {
        return -1;
    }

    buf[0] = MQTT_PKT_CONNECT;
    memcpy(buf + 1, rem_bytes, rem_used);
    memcpy(buf + 1 + rem_used, body, body_len);
    return pkt_len;
}

/*******************************************************************
** 函数名	: mqtt_build_subscribe
** 函数描述	: 组SUBSCRIBE报文(单topic, 报文标识自动递增)
** 参数		: [out] buf: 输出缓冲
**          : [in] buflen: 缓冲大小
**          : [in] topic: 主题
**          : [in] qos: 订阅质量
** 返回		: 报文长度, -1缓冲不足
********************************************************************/
int mqtt_build_subscribe(uint8_t *buf, int buflen, const char *topic, int qos)
{
    uint8_t body[256];
    uint8_t rem_bytes[4];
    int pkt_len, rem_used, body_len = 0;
    int topic_len = strlen(topic);

    body[body_len++] = (uint8_t)(s_pkt_id >> 8);
    body[body_len++] = (uint8_t)(s_pkt_id & 0xFF);
    s_pkt_id++;
    body_len += utf8_str_put(body + body_len, topic, topic_len);
    body[body_len++] = (uint8_t)qos;

    rem_used = remlen_encode(rem_bytes, body_len);
    pkt_len = 1 + rem_used + body_len;
    if (pkt_len > buflen) {
        return -1;
    }

    buf[0] = MQTT_PKT_SUBSCRIBE;        /* 0x82, 低4位固定为2 */
    memcpy(buf + 1, rem_bytes, rem_used);
    memcpy(buf + 1 + rem_used, body, body_len);
    return pkt_len;
}

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
                       const uint8_t *payload, int plen)
{
    uint8_t rem_bytes[4];
    int rem_len, rem_used, topic_len = strlen(topic);

    rem_len = 2 + topic_len + plen;
    rem_used = remlen_encode(rem_bytes, rem_len);
    if (1 + rem_used + rem_len > buflen) {
        return -1;
    }

    buf[0] = MQTT_PKT_PUBLISH;          /* QoS0, dup/retain均为0 */
    memcpy(buf + 1, rem_bytes, rem_used);
    utf8_str_put(buf + 1 + rem_used, topic, topic_len);
    if (plen > 0) {
        memcpy(buf + 1 + rem_used + 2 + topic_len, payload, plen);
    }
    return 1 + rem_used + rem_len;
}

/*******************************************************************
** 函数名	: mqtt_build_pingreq
** 函数描述	: 组PINGREQ报文
** 参数		: [out] buf: 输出缓冲(至少2字节)
** 返回		: 报文长度(2)
********************************************************************/
int mqtt_build_pingreq(uint8_t *buf)
{
    buf[0] = MQTT_PKT_PINGREQ;
    buf[1] = 0;
    return 2;
}

/*******************************************************************
** 函数名	: mqtt_reasm_feed
** 函数描述	: 链路负载喂入重组缓冲(可能跨包分片)
** 参数		: [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
void mqtt_reasm_feed(const uint8_t *data, int len)
{
    int room = MQTT_REASM_SIZE - s_reasm_len;

    if (len > room) {
        /* 溢出整包丢弃, 帧边界无法定位 */
        printf("mqtt reasm overflow, reset\r\n");
        s_reasm_len = 0;
        return;
    }
    memcpy(s_reasm + s_reasm_len, data, len);
    s_reasm_len += len;
}

/*******************************************************************
** 函数名	: mqtt_reasm_next
** 函数描述	: 从重组缓冲查看下一整帧(不移除, body指针仅consume前有效)
** 参数		: [out] f: 帧结构
** 返回		: 0取出一帧, -1数据不完整
********************************************************************/
int mqtt_reasm_next(mqtt_frame_t *f)
{
    int rem_len, rem_used, frame_len;

    if (s_reasm_len < 2) {
        return -1;
    }
    if (remlen_decode(s_reasm + 1, s_reasm_len - 1,
                      &rem_len, &rem_used) != 0) {
        return -1;
    }

    frame_len = 1 + rem_used + rem_len;
    if (frame_len > s_reasm_len) {
        return -1;              /* 帧没凑齐, 等下次 */
    }

    f->type = s_reasm[0];
    f->remlen = rem_len;
    f->body = s_reasm + 1 + rem_used;
    s_frame_total = frame_len;
    return 0;
}

/*******************************************************************
** 函数名	: mqtt_reasm_consume
** 函数描述	: 移除mqtt_reasm_next刚返回的一帧, 剩余前移
** 参数		: 无
** 返回		: 无
********************************************************************/
void mqtt_reasm_consume(void)
{
    if (s_frame_total <= 0 || s_frame_total > s_reasm_len) {
        s_frame_total = 0;
        return;
    }
    memmove(s_reasm, s_reasm + s_frame_total, s_reasm_len - s_frame_total);
    s_reasm_len -= s_frame_total;
    s_frame_total = 0;
}

/*******************************************************************
** 函数名	: mqtt_parse_publish
** 函数描述	: 解析PUBLISH帧的topic与负载
** 参数		: [in] f: 帧结构
**          : [out] pub: 字段结构
** 返回		: 0成功, -1格式非法
********************************************************************/
int mqtt_parse_publish(const mqtt_frame_t *f, mqtt_pub_t *pub)
{
    int topic_len, payload_off;
    const uint8_t *body = f->body;

    pub->qos = (f->type >> 1) & 0x03;
    if (f->remlen < 2) {
        return -1;
    }
    topic_len = (body[0] << 8) | body[1];
    payload_off = 2 + topic_len;
    if (topic_len <= 0 || topic_len > (int)sizeof(s_topic_tmp) - 1 ||
        payload_off > f->remlen) {
        return -1;
    }

    memcpy(s_topic_tmp, body + 2, topic_len);
    s_topic_tmp[topic_len] = '\0';
    pub->topic = s_topic_tmp;

    if (pub->qos > 0) {
        payload_off += 2;       /* QoS1带报文标识, 本工程不用 */
    }
    pub->payload = body + payload_off;
    pub->payload_len = f->remlen - payload_off;
    return pub->payload_len >= 0 ? 0 : -1;
}

/*******************************************************************
** 函数名	: mqtt_type_name
** 函数描述	: 报文类型转可读名(供打印用)
** 参数		: [in] type: 首字节
** 返回		: 类型名字符串
********************************************************************/
const char *mqtt_type_name(uint8_t type)
{
    switch (type & 0xF0) {
    case 0x20: return "CONNACK";
    case 0x30: return "PUBLISH";
    case 0x40: return "PUBACK";
    case 0x90: return "SUBACK";
    case 0xD0: return "PINGRESP";
    default:   return "OTHER";
    }
}
