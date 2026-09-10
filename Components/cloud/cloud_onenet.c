#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "osal.h"
#include "drv_systick.h"
#include "link.h"
#include "ntp_mmi.h"
#include "cloud_agent.h"
#include "cloud_onenet_config.h"
#include "onenet_token.h"
#include "mqtt_mmi.h"
#include "product_def.h"
#include "fw_upgrade.h"

/*
 * cloud_agent的OneNET实现（原onenet_mmi异步状态机）：
 * wifi_pro同步阻塞流程(esp_at_pump)改为软件定时器10ms/100ms轮询，
 * 发送/接收统一走链路抽象link_if_t，接入/鉴权参数由cloud_init(cfg)传入。
 *
 * 接入流程: IDLE等链路就绪 -> open TCP -> CONNECT帧 -> CONNACK
 * -> 延时 -> 4个topic依次订阅(各等SUBACK) -> 延时首报 -> 在线
 * 在线: 10s属性上报 / 30s PINGREQ心跳
 * 本地属性由cloud_set_property写入, 平台下发经on_set通知App层执行
 */

/* 属性值类型PROP_INT/PROP_FLOAT/PROP_BOOL见product_def.h */

typedef struct {
    const char *key;
    int type;
    int writable;
    int int_val;        /* PROP_INT的当前值 */
    float float_val;    /* PROP_FLOAT的当前值 */
    int bool_val;       /* PROP_BOOL的当前值 */
} prop_t;

/* 物模型属性表：由Config/product_def.h宏展开生成(编译期注册) */
#undef PROPERTY_DEF
#define PROPERTY_DEF(_KEY_, _TYPE_, _WRITABLE_, _INT_DEF_, _FLOAT_DEF_) \
    { _KEY_, _TYPE_, _WRITABLE_, _INT_DEF_, _FLOAT_DEF_, 0 },
static prop_t s_props[] = {
    PRODUCT_PROPERTY_TABLE
};
#undef PROPERTY_DEF
#define PROP_NUM   (sizeof(s_props) / sizeof(s_props[0]))

/* 订阅的4个topic模板 */
static const char *s_sub_topics[] = {
    ONENET_TOPIC_POST_REPLY,
    ONENET_TOPIC_EVENT_REPLY,
    ONENET_TOPIC_SET,
    ONENET_TOPIC_DESIRED_REPLY,
};
#define SUB_TOPIC_NUM (sizeof(s_sub_topics) / sizeof(s_sub_topics[0]))

/* 预拼好的比对topic, 收消息时直接strcmp */
static char s_topic_post_reply[ONENET_TOPIC_MAX_LEN];
static char s_topic_event_reply[ONENET_TOPIC_MAX_LEN];
static char s_topic_set[ONENET_TOPIC_MAX_LEN];
static char s_topic_desired_reply[ONENET_TOPIC_MAX_LEN];

/* 接入状态机步骤 */
typedef enum {
    ON_STEP_IDLE = 0,       /* 等链路层初始化就绪 */
    ON_STEP_TCP_START,      /* 发起open TCP(发送后等待) */
    ON_STEP_TCP_WAIT,       /* 等open结果事件回调 */
    ON_STEP_CONNECT,        /* 组CONNECT帧发送(转CONNACK) */
    ON_STEP_CONNACK,        /* 等CONNACK帧 */
    ON_STEP_SUB_DELAY,      /* CONNACK后延时 */
    ON_STEP_SUB,            /* 订阅流程 */
    ON_STEP_DESIRED,        /* 上线前延时+拉取desired */
    ON_STEP_ONLINE,         /* 在线周期任务 */
    ON_STEP_RECONN,         /* 重连延时 */
} ON_STEP_E;

static INT8U s_tmr;
static ON_STEP_E s_step;
static uint8_t s_online;
static uint8_t s_tx_busy;           /* 链路发送事务占用(缓冲互斥) */
static uint8_t s_send_result;       /* 事务发送结果: 0等待,1成功,2失败/超时 */
static uint8_t s_connack;           /* CONNACK标志(IPD解析置位) */
static uint8_t s_suback_cnt;        /* 已收SUBACK计数 */
static uint8_t s_sub_idx;           /* 已发送SUB数 */
static uint8_t s_ping_pending;      /* PINGREQ已发等PINGRESP */
static uint8_t s_ping_due;          /* 心跳到期待发(busy让路后补发) */
static INT16U s_poll;               /* 10ms轮询计数 */
static uint32_t s_last_report;      /* 上报/心跳时间基准(SYSTICK ms) */
static uint32_t s_last_ping;
static cloud_state_t s_state;      /* 接入状态记录 */
static uint32_t s_reconn_cnt;       /* 断链重连累计次数(上电起) */

/* 发送/JSON缓冲 */
static uint8_t s_mqtt_tx[MQTT_REASM_SIZE];  /* MQTT组帧缓冲(事务+发布共用, busy互斥) */
static char s_json[800];                    /* JSON负载构建缓冲 */
static char s_token[ONENET_TOKEN_MAX_LEN];  /* 鉴权token */
static char s_msg[600];                     /* 平台消息文本缓冲 */
static char s_platform_id[32] = "0";        /* 平台下发set的id, 应答时回填 */

/* 接入配置(cloud_init保存引用, 要求字符串静态生命周期)与属性下发回调 */
static cloud_cfg_t s_cfg;
static cloud_on_set_t s_on_set;

static int g_msg_id = 1;                    /* 上报消息id自增 */

/* 前置声明 */
static void OnenetClose(void);
static void OnenetIpdData(const uint8_t *data, uint16_t len);
static void OnenetPingRespSend(void);

/*******************************************************************
** 函数名	: OnenetStateName
** 函数描述	: 接入状态枚举转可打印名称
** 参数		: [in] st: 状态枚举
** 返回		: 状态名称字符串
********************************************************************/
static const char *OnenetStateName(cloud_state_t st)
{
    switch (st) {
    case CLOUD_ST_TCP:       return "tcp";
    case CLOUD_ST_CONNECT:   return "connect";
    case CLOUD_ST_SUBSCRIBE: return "subscribe";
    case CLOUD_ST_ONLINE:    return "online";
    case CLOUD_ST_RECONN:    return "reconn";
    default:                  return "offline";
    }
}

/*******************************************************************
** 函数名	: OnenetSetState
** 函数描述	: 更新接入状态记录，变化时打印日志
** 参数		: [in] st: 新状态
** 返回		: 无
********************************************************************/
static void OnenetSetState(cloud_state_t st)
{
    if (s_state != st) {
        s_state = st;
        printf("[onenet] state: %s\r\n", OnenetStateName(st));
    }
}

/*******************************************************************
** 函数名	: OnenetLinkEvCb
** 函数描述	: 链路事件回调：open结果驱动状态机，IPD数据喂重组缓冲，
**			被动断链立即收尾重连，重连延时中的关闭事件忽略
** 参数		: [in] ev: 事件类型
**          : [in] data: 负载数据
**          : [in] len: 数据长度
** 返回		: 无
********************************************************************/
static void OnenetLinkEvCb(link_event_t ev, const uint8_t *data, int len)
{
    switch (ev) {
    case LINK_EV_CONNECTED:
        s_send_result = 1;
        break;

    case LINK_EV_OPEN_FAIL:
        s_send_result = 2;
        break;

    case LINK_EV_DATA:
        OnenetIpdData(data, (uint16_t)len);
        break;

    case LINK_EV_CLOSED:
        if (s_step != ON_STEP_IDLE && s_step != ON_STEP_RECONN) {
            printf("[onenet] link closed by server, reconnect\r\n");
            OnenetClose();
        }
        break;

    default:
        break;
    }
}

/*******************************************************************
** 函数名	: OnenetTxDone
** 函数描述	: 周期上报/事件等fire-and-forget发送结果回调：
**			释放发送占用，在线期间发送失败视为链路异常收尾重连
** 参数		: [in] ok: 发送结果
** 返回		: 无
********************************************************************/
static void OnenetTxDone(int ok)
{
    s_tx_busy = 0;
    //memset(s_mqtt_tx, 0, sizeof(s_mqtt_tx));// 1111
    if (!ok && s_step == ON_STEP_ONLINE) {
        printf("[onenet] publish send failed, reconnect\r\n");
        OnenetClose();
    }
}

/*******************************************************************
** 函数名	: OnenetCmdDone
** 函数描述	: 接入流程事务发送结果回调：记录结果供状态机推进
** 参数		: [in] ok: 发送结果
** 返回		: 无
********************************************************************/
static void OnenetCmdDone(int ok)
{
    s_tx_busy = 0;
    //memset(s_mqtt_tx, 0, sizeof(s_mqtt_tx)); // 1111
    s_send_result = ok ? 1 : 2;
}

/*******************************************************************
** 函数名	: OnenetCmdSend
** 函数描述	: s_mqtt_tx缓冲内容作为链路事务发送
** 参数		: [in] len: 帧长度
** 返回		: 1入队成功, 0失败
********************************************************************/
static uint8_t OnenetCmdSend(uint16_t len)
{
    if (link_get()->send(s_mqtt_tx, len, OnenetCmdDone)) {
        s_tx_busy = 1;
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: OnenetPublish
** 函数描述	: 组PUBLISH帧发链路(busy时放弃)
** 参数		: [in] topic: 主题
**          : [in] payload: 消息负载
**          : [in] plen: 负载长度
** 返回		: 1发送入队, 0失败
********************************************************************/
static uint8_t OnenetPublish(const char *topic, const char *payload, int plen)
{
    int pkt_len;

    if (s_tx_busy) {
        printf("onenet tx busy, publish dropped\r\n");
        return 0;
    }
    printf("mqtt tx: %s\r\n", topic);
    pkt_len = mqtt_build_publish(s_mqtt_tx, sizeof(s_mqtt_tx), topic,
                                 (const uint8_t *)payload, plen);
    if (pkt_len <= 0) {
        printf("publish pack failed, topic %s\r\n", topic);
        return 0;
    }
    if (link_get()->send(s_mqtt_tx, pkt_len, OnenetTxDone)) {
        s_tx_busy = 1;
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: json_append
** 函数描述	: 向缓冲追加格式化文本, 越界自动截断
** 参数		: [in] buf: 目标缓冲
**          : [in] size: 缓冲大小
**          : [in] off: 当前偏移
**          : [in] fmt: 格式化串
** 返回		: 新偏移
********************************************************************/
static int json_append(char *buf, int size, int off, const char *fmt, ...)
{
    va_list arg_list;
    int written;

    if (off < 0 || off >= size - 1) {
        return off;
    }
    va_start(arg_list, fmt);
    written = vsnprintf(buf + off, size - off, fmt, arg_list);
    va_end(arg_list);
    if (written < 0) {
        return off;
    }
    off += written;
    if (off >= size) {
        off = size - 1;
    }
    return off;
}

/*******************************************************************
** 函数名	: FloatToStr
** 函数描述	: 浮点转1位小数字符串(MicroLIB无%f格式化, 手工拆整数小数)
** 参数		: [in] v: 浮点值
**          : [out] buf: 输出缓冲
**          : [in] size: 缓冲大小
** 返回		: 写入长度
********************************************************************/
static int FloatToStr(float v, char *buf, int size)
{
    int ip = (int)v;                    /* 向零截断 */
    int dec = (int)((v - (float)ip) * 10.0f + 0.5f);

    if (dec >= 10) {
        dec = 0;
        ip++;
    }
    if (dec < 0) {
        dec = -dec;
    }
    if (v < 0.0f && ip == 0) {
        return snprintf(buf, size, "-%d.%d", 0, dec);
    }
    return snprintf(buf, size, "%d.%d", ip, dec);
}

/*******************************************************************
** 函数名	: prop_value_str
** 函数描述	: 属性值转JSON文本(数字不引号, bool小写)
** 参数		: [in] prop: 属性
**          : [out] buf: 输出缓冲
**          : [in] size: 缓冲大小
** 返回		: 写入长度
********************************************************************/
static int prop_value_str(const prop_t *prop, char *buf, int size)
{
    switch (prop->type) {
    case PROP_BOOL:
        return snprintf(buf, size, "%s", prop->bool_val ? "true" : "false");
    case PROP_FLOAT:
        return FloatToStr(prop->float_val, buf, size);
    default:
        return snprintf(buf, size, "%d", prop->int_val);
    }
}

/*******************************************************************
** 函数名	: build_property_post
** 函数描述	: 组属性上报JSON(全部属性, 各带value+time)
** 参数		: [out] buf: 输出缓冲
**          : [in] size: 缓冲大小
** 返回		: JSON长度, -1失败
********************************************************************/
static int build_property_post(char *buf, int size)
{
    unsigned long long ts = (unsigned long long)NTP_MMI_NowMs();
    int off = 0;
    char vstr[32];
    int i;

    off = json_append(buf, size, off,
                      "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{",
                      g_msg_id++);
    for (i = 0; i < PROP_NUM; i++) {
        prop_value_str(&s_props[i], vstr, sizeof(vstr));
        off = json_append(buf, size, off, "%s\"%s\":{\"value\":%s}",
                          i ? "," : "", s_props[i].key, vstr);
        // off = json_append(buf, size, off, "%s\"%s\":{\"value\":%s,\"time\":%llu}",
        //                   i ? "," : "", s_props[i].key, vstr, ts);
    }
    off = json_append(buf, size, off, "}}");
    return off > 0 && off < size ? off : -1;
}

/*******************************************************************
** 函数名	: build_set_reply
** 函数描述	: 组属性设置应答JSON, id回填平台下发的id
** 参数		: [out] buf: 输出缓冲
**          : [in] size: 缓冲大小
**          : [in] code: 应答码(200/400)
** 返回		: JSON长度, -1失败
********************************************************************/
static int build_set_reply(char *buf, int size, int code)
{
    int off;

    off = json_append(buf, size, 0,
                      "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
                      s_platform_id, code, code == 200 ? "success" : "failed");
    return off > 0 && off < size ? off : -1;
}

/*******************************************************************
** 函数名	: build_desired_get
** 函数描述	: 组读取desired值请求JSON, 平台要求params为属性名数组
** 参数		: [out] buf: 输出缓冲
**          : [in] size: 缓冲大小
** 返回		: JSON长度, -1失败
********************************************************************/
static int build_desired_get(char *buf, int size)
{
    int off;
    int first = 1;
    int i;

    off = json_append(buf, size, 0, "{\"id\":\"%d\",\"version\":\"1.0\","
                      "\"params\":[", g_msg_id++);
    for (i = 0; i < PROP_NUM; i++) {
        if (!s_props[i].writable) {
            continue;
        }
        off = json_append(buf, size, off, "%s\"%s\"",
                          first ? "" : ",", s_props[i].key);
        first = 0;
    }
    off = json_append(buf, size, off, "]}");
    return off > 0 && off < size ? off : -1;
}

/*******************************************************************
** 函数名	: json_find_key
** 函数描述	: 定位"key":的位置, 返回值指向冒号后
** 参数		: [in] json: JSON文本
**          : [in] key: 字段名
** 返回		: 冒号后位置, 未找到NULL
********************************************************************/
static const char *json_find_key(const char *json, const char *key)
{
    char pat[64];
    const char *pos = json;
    int pat_len;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    pat_len = (int)strlen(pat);
    while ((pos = strstr(pos, pat)) != 0) {
        const char *after_key = pos + pat_len;

        while (*after_key == ' ' || *after_key == '\t') {
            after_key++;
        }
        if (*after_key == ':') {
            return after_key + 1;
        }
        pos += pat_len;
    }
    return 0;
}

/*******************************************************************
** 函数名	: OnenetAtof
** 函数描述	: 字符串转浮点(MicroLIB的strtod替代, 支持负数小数)
** 参数		: [in] s: 数字串
** 返回		: 数值
********************************************************************/
static double OnenetAtof(const char *s)
{
    double val = 0.0;
    int neg = 0;

    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        double scale = 0.1;

        s++;
        while (*s >= '0' && *s <= '9') {
            val += (*s - '0') * scale;
            scale *= 0.1;
            s++;
        }
    }
    return neg ? -val : val;
}

/*******************************************************************
** 函数名	: json_get_number
** 函数描述	: 读取数值字段, 兼容数字或引号包裹
** 参数		: [in] json: JSON文本
**          : [in] key: 字段名
**          : [out] out: 数值
** 返回		: 0成功, -1未找到
********************************************************************/
static int json_get_number(const char *json, const char *key, double *out)
{
    const char *pos = json_find_key(json, key);

    if (pos == 0) {
        return -1;
    }
    while (*pos == ' ') {
        pos++;
    }
    if (*pos == '"') {
        pos++;
    }
    if (*pos == 't') {              /* true记1 */
        *out = 1;
        return 0;
    }
    if (*pos == 'f') {              /* false记0 */
        *out = 0;
        return 0;
    }
    *out = OnenetAtof(pos);
    return 0;
}

/*******************************************************************
** 函数名	: json_get_string
** 函数描述	: 读取字符串字段
** 参数		: [in] json: JSON文本
**          : [in] key: 字段名
**          : [out] out: 输出缓冲
**          : [in] out_size: 缓冲大小
** 返回		: 0成功, -1未找到
********************************************************************/
static int json_get_string(const char *json, const char *key,
                           char *out, int out_size)
{
    const char *pos = json_find_key(json, key);
    int copied = 0;

    if (pos == 0) {
        return -1;
    }
    while (*pos == ' ') {
        pos++;
    }
    if (*pos != '"') {
        return -1;
    }
    pos++;
    while (*pos && *pos != '"' && copied < out_size - 1) {
        out[copied++] = *pos++;
    }
    out[copied] = '\0';
    return 0;
}

/*******************************************************************
** 函数名	: parse_reply_json
** 函数描述	: 解析平台应答, 兼容errcode/code与errmsg/msg两套字段
** 参数		: [in] json: 应答JSON
**          : [out] err_code: 错误码
**          : [out] err_msg: 错误消息
**          : [in] msg_size: 消息缓冲大小
** 返回		: 0成功, -1失败
********************************************************************/
static int parse_reply_json(const char *json, int *err_code,
                            char *err_msg, int msg_size)
{
    double val;

    *err_code = -1;
    err_msg[0] = '\0';

    /* 平台应答的id为数字, 取最大值后自增, 保持id不重 */
    if (json_get_number(json, "id", &val) == 0 && (int)val >= g_msg_id) {
        g_msg_id = (int)val + 1;
    }

    if (json_get_number(json, "errcode", &val) != 0 &&
        json_get_number(json, "code", &val) != 0) {
        return -1;
    }
    *err_code = (int)val;

    if (json_get_string(json, "errmsg", err_msg, msg_size) != 0) {
        json_get_string(json, "msg", err_msg, msg_size);
    }
    return 0;
}

/*******************************************************************
** 函数名	: prop_apply
** 函数描述	: 属性值写入物模型(按类型分发)，并通知上层on_set回调
** 参数		: [in] prop: 属性
**          : [in] value: 新值
** 返回		: 无
********************************************************************/
static void prop_apply(prop_t *prop, double value)
{
    char vbuf[24];

    switch (prop->type) {
    case PROP_BOOL:
        prop->bool_val = value != 0;
        snprintf(vbuf, sizeof(vbuf), "%s", prop->bool_val ? "true" : "false");
        printf("prop %s <- %s\r\n", prop->key, vbuf);
        break;
    case PROP_FLOAT:
        prop->float_val = (float)value;
        FloatToStr(prop->float_val, vbuf, sizeof(vbuf));
        printf("prop %s <- %s\r\n", prop->key, vbuf);
        break;
    default:
        prop->int_val = (int)value;
        snprintf(vbuf, sizeof(vbuf), "%d", prop->int_val);
        printf("prop %s <- %d\r\n", prop->key, prop->int_val);
        break;
    }

    /* 通知上层(物模型层)属性已更新 */
    if (s_on_set != 0) {
        s_on_set(prop->key, vbuf);
    }
}

/*******************************************************************
** 函数名	: parse_set_json
** 函数描述	: 解析平台属性设置, 平台格式为params下直接值
** 参数		: [in] json: 设置JSON
** 返回		: 0有属性被应用, -1无效设置
********************************************************************/
static int parse_set_json(const char *json)
{
    int applied = 0;
    int i;

    json_get_string(json, "id", s_platform_id, sizeof(s_platform_id));

    for (i = 0; i < PROP_NUM; i++) {
        double set_val;

        if (!s_props[i].writable) {
            continue;
        }
        if (json_get_number(json, s_props[i].key, &set_val) == 0) {
            prop_apply(&s_props[i], set_val);
            applied++;
        }
    }
    return applied > 0 ? 0 : -1;
}

/*******************************************************************
** 函数名	: json_get_nested_number
** 函数描述	: 读取"key":{..."value":v格式的嵌套值(属性值包含格式)
** 参数		: [in] json: JSON文本
**          : [in] key: 属性名
**          : [out] out: 数值
** 返回		: 0成功, -1未找到
********************************************************************/
static int json_get_nested_number(const char *json, const char *key,
                                  double *out)
{
    char pat[64];
    const char *pos;

    snprintf(pat, sizeof(pat), "\"%s\":{", key);
    pos = strstr(json, pat);
    if (pos == 0) {
        /* 有的回应直接给值 */
        snprintf(pat, sizeof(pat), "\"%s\"", key);
        pos = strstr(json, pat);
        if (pos == 0) {
            return -1;
        }
    }
    pos = strstr(pos, "\"value\"");
    if (pos == 0) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (pos == 0) {
        return -1;
    }
    pos++;
    while (*pos == ' ') {
        pos++;
    }
    if (*pos == 't') {
        *out = 1;
        return 0;
    }
    if (*pos == 'f') {
        *out = 0;
        return 0;
    }
    if (*pos == '"') {
        pos++;
    }
    *out = OnenetAtof(pos);
    return 0;
}

/*******************************************************************
** 函数名	: parse_desired_reply
** 函数描述	: 解析desired值回应, 把各可写属性期望值应用到物模型
** 参数		: [in] json: 回应JSON
** 返回		: 0有属性被应用, -1无效回应
********************************************************************/
static int parse_desired_reply(const char *json)
{
    double code;
    int applied = 0;
    int i;

    if (json_get_number(json, "code", &code) == 0 &&
        (int)code != 0 && (int)code != 200) {
        printf("desired get refused, code=%d\r\n", (int)code);
        return -1;
    }

    /* 期望值在data字段里, 先定位该字段, 在其内找value */
    {
        const char *data_field = json_find_key(json, "data");

        if (data_field != 0) {
            json = data_field;
        }
    }
    for (i = 0; i < PROP_NUM; i++) {
        double desired_val;

        if (!s_props[i].writable) {
            continue;
        }
        if (json_get_nested_number(json, s_props[i].key, &desired_val) == 0) {
            char vbuf[24];

            FloatToStr((float)desired_val, vbuf, sizeof(vbuf));
            printf("desired %s = %s\r\n", s_props[i].key, vbuf);
            prop_apply(&s_props[i], desired_val);
            applied++;
        }
    }
    if (applied == 0) {
        printf("desired reply has no writable props\r\n");
    }
    return applied > 0 ? 0 : -1;
}

/*******************************************************************
** 函数名	: OnenetMessage
** 函数描述	: 平台下发PUBLISH按topic分发处理
** 参数		: [in] pub: PUBLISH字段
** 返回		: 无
********************************************************************/
static void OnenetMessage(const mqtt_pub_t *pub)
{
    int len = pub->payload_len < (int)sizeof(s_msg) - 1
            ? pub->payload_len : (int)sizeof(s_msg) - 1;

    memcpy(s_msg, pub->payload, len);
    s_msg[len] = '\0';
    printf("platform msg on %s\r\n  payload: %s\r\n", pub->topic, s_msg);

    if (strcmp(pub->topic, s_topic_post_reply) == 0 ||
        strcmp(pub->topic, s_topic_event_reply) == 0) {
        int code = -1;
        char errmsg[64];

        if (parse_reply_json(s_msg, &code, errmsg, sizeof(errmsg)) == 0) {
            printf("report %s, errcode=%d errmsg=%s\r\n",
                   code == 0 || code == 200 ? "ok" : "failed", code, errmsg);
        } else {
            printf("reply unparsed: %s\r\n", s_msg);
        }
    } else if (strcmp(pub->topic, s_topic_set) == 0) {
        char reply[160];
        char reply_topic[ONENET_TOPIC_MAX_LEN];
        int reply_len = build_set_reply(reply, sizeof(reply),
                                        parse_set_json(s_msg) == 0 ? 200 : 400);

        if (reply_len > 0) {
            snprintf(reply_topic, sizeof(reply_topic), ONENET_TOPIC_SET_REPLY,
                     s_cfg.product_id, s_cfg.device_name);
            OnenetPublish(reply_topic, reply, reply_len);
        }
    } else if (strcmp(pub->topic, s_topic_desired_reply) == 0) {
        parse_desired_reply(s_msg);
    } else {
        printf("unmatched topic, %d bytes ignored\r\n", pub->payload_len);
    }
}

/*******************************************************************
** 函数名	: OnenetFrame
** 函数描述	: MQTT帧分发：连接应答/订阅应答/心跳应答/下发消息
** 参数		: [in] f: 帧结构
** 返回		: 无
********************************************************************/
static void OnenetFrame(const mqtt_frame_t *f)
{
    switch (f->type & 0xF0) {
    case 0x20:                  /* CONNACK */
        if (f->remlen >= 2 && f->body[1] == 0) {
            s_connack = 1;
            printf("mqtt connected (CONNACK)\r\n");
        } else {
            printf("connack refused, code=%d\r\n",
                   f->remlen >= 2 ? f->body[1] : -1);
        }
        break;
    case 0x90:                  /* SUBACK */
        s_suback_cnt++;
        printf("suback %u\r\n", (unsigned int)s_suback_cnt);
        break;
    case 0xC0:                  /* 平台主动PINGREQ探测: 回PINGRESP保活 */
        printf("[onenet] pingreq from server, reply pingresp\r\n");
        OnenetPingRespSend();
        break;
    case 0xD0:                  /* PINGRESP */
        s_ping_pending = 0;
        break;
    case 0x30: {                /* PUBLISH下发 */
        mqtt_pub_t pub;

        if (mqtt_parse_publish(f, &pub) == 0) {
            OnenetMessage(&pub);
        }
        break;
    }
    default:
        break;
    }
}

/*******************************************************************
** 函数名	: OnenetIpdData
** 函数描述	: 链路数据回调：喂重组缓冲并逐帧分发
**			(原始数据hex打印已关闭, 调试需开启时取消注释)
** 参数		: [in] data: 负载数据
**          : [in] len: 负载长度
** 返回		: 无
********************************************************************/
static void OnenetIpdData(const uint8_t *data, uint16_t len)
{
    mqtt_frame_t f;

    /* 打印MQTT接收原始数据(hex, 字符串形式由esp_wifi_recv层统一打印) */
    // printf("[mqtt] ipd raw(%u):\r\n", (unsigned int)len);
    // for (i = 0; i < len; i++) {
    //     printf("%02X ", data[i]);
    //     if ((i & 0xf) == 0xf) {
    //         printf("\r\n");
    //     }
    // }
    // if ((len & 0xf) != 0) {
    //     printf("\r\n");
    // }

    mqtt_reasm_feed(data, len);
    while (mqtt_reasm_next(&f) == 0) {
        printf("mqtt rx: %s, %d bytes\r\n", mqtt_type_name(f.type), f.remlen);
        OnenetFrame(&f);
        mqtt_reasm_consume();
    }
}

/*******************************************************************
** 函数名	: OnenetClose
** 函数描述	: 接入失败/断链收尾：关闭链路后延时重连
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetClose(void)
{
    s_online = 0;
    s_connack = 0;
    s_suback_cnt = 0;
    s_ping_pending = 0;
    s_ping_due = 0;
    s_send_result = 0;
    OnenetSetState(CLOUD_ST_RECONN);

    /* 重连计数+1并打印(覆盖断链/接入失败/WiFi掉线所有收尾场景) */
    s_reconn_cnt++;
    printf("[onenet] reconnect count: %u\r\n", (unsigned int)s_reconn_cnt);

    /* 关闭链路(fire-and-forget), 直接进入重连延时 */
    link_get()->close();

    s_poll = 0;
    s_step = ON_STEP_RECONN;
    osal_timer_start(s_tmr, 100, 1);
}

/*******************************************************************
** 函数名	: OnenetTcpStart
** 函数描述	: 经链路抽象发起TCP连接, 结果由链路事件回调通知
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetTcpStart(void)
{
    OnenetSetState(CLOUD_ST_TCP);

    s_send_result = 0;
    if (!link_get()->open(s_cfg.host, s_cfg.port, OnenetLinkEvCb)) {
        printf("[onenet] open rejected, wait link ready\r\n");
        s_step = ON_STEP_IDLE;
        osal_timer_start(s_tmr, 100, 1);
        return;
    }
    printf("[onenet] connecting %s:%u\r\n",
           s_cfg.host, (unsigned int)s_cfg.port);
    s_step = ON_STEP_TCP_WAIT;
}

/*******************************************************************
** 函数名	: OnenetConnectSend
** 函数描述	: 生成token并组CONNECT帧发送, 转CONNACK等待
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetConnectSend(void)
{
    uint32_t expire_ts;
    int pkt_len;

    OnenetSetState(CLOUD_ST_CONNECT);

    /* et: NTP有效则当前时间+30天, 未校时用固定兜底时间戳 */
    if (NTP_MMI_TimeValid()) {
        expire_ts = NTP_MMI_NowUnix() + ONENET_TOKEN_VALID_SEC;
    } else {
        expire_ts = ONENET_TOKEN_ET_FALLBACK;
    }
    if (onenet_token_build(expire_ts, s_cfg.product_id, s_cfg.device_name,
                           s_cfg.access_key, s_token, sizeof(s_token)) != 0) {
        printf("token build failed\r\n");
        OnenetClose();
        return;
    }
    printf("token: %s\r\n", s_token);

    pkt_len = mqtt_build_connect(s_mqtt_tx, sizeof(s_mqtt_tx), s_cfg.client_id,
                                 s_cfg.username, s_token, ONENET_KEEPALIVE_SEC);
    if (pkt_len <= 0 || !OnenetCmdSend((uint16_t)pkt_len)) {
        printf("connect send failed\r\n");
        OnenetClose();
        return;
    }
    s_send_result = 0;
    s_connack = 0;
    s_poll = 0;
    s_step = ON_STEP_CONNACK;
}

/*******************************************************************
** 函数名	: OnenetSubSend
** 函数描述	: 组下一个topic的SUBSCRIBE帧发送, 转SUB等待
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetSubSend(void)
{
    char topic[ONENET_TOPIC_MAX_LEN];
    int pkt_len;

    OnenetSetState(CLOUD_ST_SUBSCRIBE);

    snprintf(topic, sizeof(topic), s_sub_topics[s_sub_idx],
             s_cfg.product_id, s_cfg.device_name);
    pkt_len = mqtt_build_subscribe(s_mqtt_tx, sizeof(s_mqtt_tx), topic, 0);
    if (pkt_len <= 0 || !OnenetCmdSend((uint16_t)pkt_len)) {
        printf("subscribe send failed\r\n");
        OnenetClose();
        return;
    }
    s_sub_idx++;
    s_send_result = 0;
    s_poll = 0;
    s_step = ON_STEP_SUB;
}

/*******************************************************************
** 函数名	: OnenetOnlineEnter
** 函数描述	: 订阅完成后拉取desired并转在线状态
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetOnlineEnter(void)
{
    char body[256];
    char topic[ONENET_TOPIC_MAX_LEN];
    int len = build_desired_get(body, sizeof(body));

    OnenetSetState(CLOUD_ST_ONLINE);

    if (len > 0) {
        snprintf(topic, sizeof(topic), ONENET_TOPIC_DESIRED_GET,
                 s_cfg.product_id, s_cfg.device_name);
        OnenetPublish(topic, body, len);
    }

    s_online = 1;
    s_last_report = SYSTICK_GetMsTick();
    s_last_ping = s_last_report;
    s_ping_due = 0;
    printf("onenet online, keepalive %us\r\n", (unsigned int)ONENET_KEEPALIVE_SEC);
}

/*******************************************************************
** 函数名	: OnenetReport
** 函数描述	: 周期属性上报
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetReport(void)
{
    char topic[ONENET_TOPIC_MAX_LEN];
    int len;

    /* OTA升级中跳过属性上报 */
    if (FW_UPG_GetState() != FW_UPG_STATE_IDLE) {
        return;
    }
    
    len = build_property_post(s_json, sizeof(s_json));
    if (len > 0) {
        snprintf(topic, sizeof(topic), ONENET_TOPIC_POST,
                 s_cfg.product_id, s_cfg.device_name);
        printf("property post (%d bytes)\r\n", len);
        OnenetPublish(topic, s_json, len);
    }
}

/*******************************************************************
** 函数名	: OnenetPing
** 函数描述	: 心跳：发PINGREQ置pending等PINGRESP
** 参数		: 无
** 返回		: 1发送入队成功, 0事务占用未发出
********************************************************************/
static uint8_t OnenetPing(void)
{
    int pkt_len = mqtt_build_pingreq(s_mqtt_tx);

    if (OnenetCmdSend((uint16_t)pkt_len)) {
        s_ping_pending = 1;
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: OnenetPingRespSend
** 函数描述	: 应答平台主动下发的PINGREQ探测(独立缓冲, 不占事务)
** 参数		: 无
** 返回		: 无
********************************************************************/
static void OnenetPingRespSend(void)
{
    static const uint8_t resp[2] = { 0xD0, 0x00 };

    /* fire-and-forget: 失败由平台重发PINGREQ兜底 */
    if (!link_get()->send(resp, 2, 0)) {
        printf("[onenet] pingresp busy, dropped\r\n");
    }
}

/*******************************************************************
** 函数名	: OnenetTmrProc
** 函数描述	: 接入状态机定时器回调：10ms/100ms步进各阶段
** 参数		: [in] pdata: 回调参数(未用)
** 返回		: 无
********************************************************************/
static void OnenetTmrProc(void *pdata)
{
    pdata = pdata;

    switch (s_step) {
    case ON_STEP_IDLE:
        /* 等链路层就绪(模块初始化完成且路由器已连接)后开始接入 */
        if (link_get()->is_ready()) {
            OnenetTcpStart();
        } else {
            osal_timer_start(s_tmr, 100, 1);
        }
        break;

    case ON_STEP_TCP_WAIT:
        /* 等open结果事件回调(链路层超时兜底) */
        if (s_send_result == 1) {
            OnenetConnectSend();
        } else if (s_send_result == 2) {
            printf("tcp connect to %s:%u failed\r\n",
                   s_cfg.host, (unsigned int)s_cfg.port);
            OnenetClose();
        }
        break;

    case ON_STEP_CONNACK:
        if (s_connack) {
            s_connack = 0;
            s_poll = 0;
            s_step = ON_STEP_SUB_DELAY;
            osal_timer_start(s_tmr, 10, 1);
        } else if (s_send_result == 2) {
            OnenetClose();
        } else if (++s_poll >= ONENET_CONNACK_TIMEOUT_MS / 10) {
            printf("connack timeout\r\n");
            OnenetClose();
        } else {
            osal_timer_start(s_tmr, 10, 1);
        }
        break;

    case ON_STEP_SUB_DELAY:
        /* 连接后稍等, 平台会话建立需要时间 */
        if (++s_poll >= ONENET_SUB_DELAY_MS / 10) {
            s_sub_idx = 0;
            s_suback_cnt = 0;
            OnenetSubSend();
        } else {
            osal_timer_start(s_tmr, 10, 1);
        }
        break;

    case ON_STEP_SUB:
        if (s_send_result == 2) {
            OnenetClose();
        } else if (s_suback_cnt >= s_sub_idx) {
            /* 本topic的SUBACK收到 */
            if (s_sub_idx >= SUB_TOPIC_NUM) {
                /* 4个订阅完成, 延时后拉desired上线 */
                s_poll = 0;
                s_step = ON_STEP_DESIRED;
                osal_timer_start(s_tmr, 10, 1);
            } else {
                OnenetSubSend();
            }
        } else if (++s_poll >= ONENET_SUBACK_TIMEOUT_MS / 10) {
            printf("suback timeout: %u\r\n", (unsigned int)s_sub_idx);
            OnenetClose();
        } else {
            osal_timer_start(s_tmr, 10, 1);
        }
        break;

    case ON_STEP_DESIRED:
        /* 上线前延迟(等平台稳定), 之后拉desired并上线 */
        if (++s_poll >= ONENET_FIRST_REPORT_DELAY_MS / 10) {
            OnenetOnlineEnter();
            s_step = ON_STEP_ONLINE;
            osal_timer_start(s_tmr, 100, 1);
        } else {
            osal_timer_start(s_tmr, 10, 1);
        }
        break;

    case ON_STEP_ONLINE: {
        uint32_t now = SYSTICK_GetMsTick();

        /* PINGREQ发送失败(链路异常)立即收尾重连, 不等PINGRESP超时 */
        if (s_send_result == 2) {
            printf("[onenet] ping send failed, link lost\r\n");
            OnenetClose();
            break;
        }

        /* 心跳到期置排队标志: 发送busy时让路, 释放后补发,
         * 避免心跳周期与上报周期撞车(30s是10s整数倍)被事务饿死 */
        if (now - s_last_ping >= ONENET_HEARTBEAT_MS) {
            s_ping_due = 1;
        }

        /* 发送缓冲空闲: 心跳优先补发, 其次周期上报 */
        if (!s_tx_busy) {
            if (s_ping_due) {
                if (OnenetPing()) {
                    /* 只有真正发出才清标志/更新时间基准 */
                    s_ping_due = 0;
                    s_last_ping = now;
                }
            } else if (now - s_last_report >= ONENET_REPORT_MS) {
                s_last_report = now;
                OnenetReport();
            }
        }
        /* PINGRESP超时判断链路丢失 */
        if (s_ping_pending && now - s_last_ping > ONENET_PINGRESP_TIMEOUT_MS) {
            printf("no pingresp, link lost\r\n");
            OnenetClose();
            break;
        }
        osal_timer_start(s_tmr, 100, 1);
        break;
    }

    case ON_STEP_RECONN:
        if (++s_poll >= 5000 / 100) {
            s_step = ON_STEP_IDLE;
            osal_timer_start(s_tmr, 100, 1);
        } else {
            osal_timer_start(s_tmr, 100, 1);
        }
        break;

    default:
        osal_timer_stop(s_tmr);
        break;
    }
}

/*******************************************************************
** 函数名	: cloud_init
** 函数描述	: 云平台接入初始化：保存配置、预拼topic、启动接入状态机，
**			须在link_init之后调用
** 参数		: [in] cfg: 接入配置(保存引用, 要求字符串静态生命周期)
**          : [in] on_set: 属性下发回调，可传0
** 返回		: 1成功, 0失败
********************************************************************/
int cloud_init(const cloud_cfg_t *cfg, cloud_on_set_t on_set)
{
    if (cfg == 0 || cfg->host == 0 || cfg->product_id == 0 ||
        cfg->device_name == 0 || cfg->access_key == 0) {
        return 0;
    }
    s_cfg = *cfg;
    s_on_set = on_set;

    snprintf(s_topic_post_reply, sizeof(s_topic_post_reply),
             ONENET_TOPIC_POST_REPLY, s_cfg.product_id, s_cfg.device_name);
    snprintf(s_topic_event_reply, sizeof(s_topic_event_reply),
             ONENET_TOPIC_EVENT_REPLY, s_cfg.product_id, s_cfg.device_name);
    snprintf(s_topic_set, sizeof(s_topic_set),
             ONENET_TOPIC_SET, s_cfg.product_id, s_cfg.device_name);
    snprintf(s_topic_desired_reply, sizeof(s_topic_desired_reply),
             ONENET_TOPIC_DESIRED_REPLY, s_cfg.product_id, s_cfg.device_name);

    g_msg_id = 1;
    s_reconn_cnt = 0;
    OnenetSetState(CLOUD_ST_OFFLINE);

    s_step = ON_STEP_IDLE;
    s_tmr = osal_timer_create((void *)0, OnenetTmrProc);
    if (s_tmr != 0xff) {
        osal_timer_start(s_tmr, 100, 1);
    }
    return 1;
}

/*******************************************************************
** 函数名	: cloud_report_property
** 函数描述	: 属性上报：JSON由上层物模型组装, 组件组topic发布
** 参数		: [in] json: 上报JSON文本
** 返回		: 1发送入队, 0失败
********************************************************************/
int cloud_report_property(const char *json)
{
    char topic[ONENET_TOPIC_MAX_LEN];

    if (json == 0 || s_step != ON_STEP_ONLINE) {
        return 0;
    }
    /* OTA升级中跳过属性上报 */
    if (FW_UPG_GetState() != FW_UPG_STATE_IDLE) {
        return 0;
    }
    snprintf(topic, sizeof(topic), ONENET_TOPIC_POST,
             s_cfg.product_id, s_cfg.device_name);
    return OnenetPublish(topic, json, (int)strlen(json));
}

/*******************************************************************
** 函数名	: cloud_post_event
** 函数描述	: 事件上报
** 参数		: [in] event_id: 事件ID
**          : [in] json: 事件参数JSON
** 返回		: 1发送入队, 0失败
********************************************************************/
int cloud_post_event(const char *event_id, const char *json)
{
    char topic[ONENET_TOPIC_MAX_LEN];
    char body[256];
    int len;

    if (event_id == 0 || json == 0 || s_step != ON_STEP_ONLINE) {
        return 0;
    }
    /* OTA升级中跳过事件上报 */
    if (FW_UPG_GetState() != FW_UPG_STATE_IDLE) {
        return 0;
    }
    snprintf(topic, sizeof(topic), ONENET_TOPIC_EVENT_POST,
             s_cfg.product_id, s_cfg.device_name);
    len = snprintf(body, sizeof(body), "{\"id\":\"%s\",\"params\":%s}",
                   event_id, json);
    if (len < 0 || len >= (int)sizeof(body)) {
        return 0;
    }
    return OnenetPublish(topic, body, len);
}

/*******************************************************************
** 函数名	: cloud_set_property
** 函数描述	: 本地属性值更新(传感器采集写入物模型, 周期上报携带)。
**			与平台下发不同, 此处只更新本地值, 不触发on_set回调
** 参数		: [in] key: 属性名(见product_def.h)
**          : [in] value: 值文本("25.5"/"45"/"true")
** 返回		: 1成功, 0未知属性
********************************************************************/
int cloud_set_property(const char *key, const char *value)
{
    int i;

    if (key == 0 || value == 0) {
        return 0;
    }
    for (i = 0; i < PROP_NUM; i++) {
        prop_t *prop = &s_props[i];

        if (strcmp(prop->key, key) != 0) {
            continue;
        }
        switch (prop->type) {
        case PROP_BOOL:
            prop->bool_val = (value[0] == 't' || value[0] == 'T' ||
                              value[0] == '1');
            break;
        case PROP_FLOAT:
            prop->float_val = (float)OnenetAtof(value);
            break;
        default:
            prop->int_val = (int)OnenetAtof(value);
            break;
        }
        return 1;
    }
    return 0;
}

/*******************************************************************
** 函数名	: cloud_is_online
** 函数描述	: 查询云平台是否已接入在线
** 参数		: 无
** 返回		: 1在线, 0未接入
********************************************************************/
uint8_t cloud_is_online(void)
{
    return s_online;
}

/*******************************************************************
** 函数名	: cloud_get_state
** 函数描述	: 查询云平台接入状态
** 参数		: 无
** 返回		: cloud_state_t状态
********************************************************************/
cloud_state_t cloud_get_state(void)
{
    return s_state;
}
