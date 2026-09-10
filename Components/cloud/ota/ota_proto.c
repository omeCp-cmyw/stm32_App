#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_proto.h"
#include "onenet_token.h"
#include "gateway_config.h"

/*
 * OneNET OTA远程升级协议层（移植自wifi_pro的proto/ota.c）：
 * 封装HTTP请求拼装与应答解析，供OTA状态机调用。
 * 所有函数只组装请求字符串，不涉及网络收发。
 */

/* JSON体, Content-Length由调用者计算长度 */
#define OTA_VERSION_BODY \
    "{\"s_version\":\"" GW_OTA_S_VERSION \
    "\", \"f_version\": \"" GW_OTA_F_VERSION "\"}"

/*******************************************************************
** 函数名	: ota_version_request
** 函数描述	: 拼装版本上报POST请求（头+JSON体），Authorization直接跟
**             MQTT接入同设备的token
** 参数		: [in] expire_ts: token过期时间(unix秒)
**          : [in] product_id: 产品ID
**          : [in] device_name: 设备名
**          : [in] access_key: 访问密钥
**          : [out] out: 请求输出缓冲
**          : [in] out_size: 缓冲大小
** 返回		: 请求字节数, -1失败
********************************************************************/
int ota_version_request(uint32_t expire_ts, const char *product_id,
                        const char *device_name, const char *access_key,
                        char *out, int out_size)
{
    char token[ONENET_TOKEN_MAX_LEN];
    const int body_len = (int)(sizeof(OTA_VERSION_BODY) - 1);
    int used;

    if (onenet_token_build(expire_ts, product_id, device_name,
                           access_key, token, sizeof(token)) != 0)
        return -1;

    used = snprintf(out, (size_t)out_size,
                    "POST /fuse-ota/%s/%s/version HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d\r\n"
                    "Authorization: %s\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n"
                    "%s",
                    product_id, device_name, GW_OTA_HOST,
                    body_len, token, OTA_VERSION_BODY);
    if (used < 0 || used >= out_size)
        return -1;
    return used;
}

/*******************************************************************
** 函数名	: ota_check_request
** 函数描述	: 拼装升级检查GET请求，type固定1取固件通道,
**             version为当前固件版本
** 参数		: [in] expire_ts: token过期时间(unix秒)
**          : [in] product_id: 产品ID
**          : [in] device_name: 设备名
**          : [in] access_key: 访问密钥
**          : [in] version: 当前固件版本
**          : [out] out: 请求输出缓冲
**          : [in] out_size: 缓冲大小
** 返回		: 请求字节数, -1失败
********************************************************************/
int ota_check_request(uint32_t expire_ts, const char *product_id,
                      const char *device_name, const char *access_key,
                      const char *version, char *out, int out_size)
{
    char token[ONENET_TOKEN_MAX_LEN];
    int used;

    if (onenet_token_build(expire_ts, product_id, device_name,
                           access_key, token, sizeof(token)) != 0)
        return -1;

    used = snprintf(out, (size_t)out_size,
                    "GET /fuse-ota/%s/%s/check?type=1&version=%s HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Authorization: %s\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n",
                    product_id, device_name, version, GW_OTA_HOST, token);
    if (used < 0 || used >= out_size)
        return -1;
    return used;
}

/*******************************************************************
** 函数名	: ota_download_request
** 函数描述	: 拼装固件下载GET请求，路径含check记录的任务tid;
**             range_end>=0时带Range: start-end头分片请求, 否则全量
** 参数		: [in] expire_ts: token过期时间(unix秒)
**          : [in] product_id: 产品ID
**          : [in] device_name: 设备名
**          : [in] access_key: 访问密钥
**          : [in] tid: 任务ID
**          : [in] range_start: 分片起始字节(含)
**          : [in] range_end: 分片结束字节(含), <0不带Range头全量下载
**          : [out] out: 请求输出缓冲
**          : [in] out_size: 缓冲大小
** 返回		: 请求字节数, -1失败
********************************************************************/
int ota_download_request(uint32_t expire_ts, const char *product_id,
                         const char *device_name, const char *access_key,
                         int tid, long range_start, long range_end,
                         char *out, int out_size)
{
    char token[ONENET_TOKEN_MAX_LEN];
    char range[48];
    int used;

    if (onenet_token_build(expire_ts, product_id, device_name,
                           access_key, token, sizeof(token)) != 0)
        return -1;

    /* 平台文档模式2: Range=start-end取start和end都包含的字节 */
    range[0] = '\0';
    if (range_end >= 0) {
        used = snprintf(range, sizeof(range), "Range: %ld-%ld\r\n",
                        range_start, range_end);
        if (used < 0 || used >= (int)sizeof(range))
            return -1;
    }
    used = snprintf(out, (size_t)out_size,
                    "GET /fuse-ota/%s/%s/%d/download HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Authorization: %s\r\n"
                    "%s"
                    "Connection: keep-alive\r\n"
                    "\r\n",
                    product_id, device_name, tid,
                    GW_OTA_HOST, token, range);
    if (used < 0 || used >= out_size)
        return -1;
    return used;
}

/*******************************************************************
** 函数名	: ota_status_request
** 函数描述	: 拼装升级状态上报POST请求，体为{"step":N};
**             step 0-99下载进度, 100升级中, >100平台状态码;
**             可选上报, 等100时平台转下载中, 不等就上报101;
**             可选下载中上报进度, 可选上报结束状态
** 参数		: [in] expire_ts: token过期时间(unix秒)
**          : [in] product_id: 产品ID
**          : [in] device_name: 设备名
**          : [in] access_key: 访问密钥
**          : [in] tid: 任务ID
**          : [in] step: 下载进度或状态码
**          : [out] out: 请求输出缓冲
**          : [in] out_size: 缓冲大小
** 返回		: 请求字节数, -1失败
********************************************************************/
int ota_status_request(uint32_t expire_ts, const char *product_id,
                       const char *device_name, const char *access_key,
                       int tid, int step, char *out, int out_size)
{
    char token[ONENET_TOKEN_MAX_LEN];
    char body[32];
    int used;
    int body_len;

    if (onenet_token_build(expire_ts, product_id, device_name,
                           access_key, token, sizeof(token)) != 0)
        return -1;

    body_len = snprintf(body, sizeof(body), "{\"step\":%d}", step);
    if (body_len < 0 || body_len >= (int)sizeof(body))
        return -1;

    used = snprintf(out, (size_t)out_size,
                    "POST /fuse-ota/%s/%s/%d/status HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d\r\n"
                    "Authorization: %s\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n"
                    "%s",
                    product_id, device_name, tid,
                    GW_OTA_HOST, body_len, token, body);
    if (used < 0 || used >= out_size)
        return -1;
    return used;
}

/*******************************************************************
** 函数名	: ota_resp_code
** 函数描述	: 从HTTP应答体中提取code字段值, OTA接口通用
** 参数		: [in] resp: 缓冲应答体文本(以'\0'结尾)
** 返回		: code值, -1未找到或解析失败
********************************************************************/
int ota_resp_code(const char *resp)
{
    const char *p = strstr(resp, "\"code\"");
    long val;
    char *endp;

    if (p == NULL)
        return -1;
    p = strchr(p, ':');
    if (p == NULL)
        return -1;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p < '0' || *p > '9')
        return -1;
    val = strtol(p, &endp, 10);
    if (endp == p)
        return -1;
    return (int)val;
}

/*******************************************************************
** 函数名	: json_find_value
** 函数描述	: 定位JSON键值对的冒号后字符串
** 参数		: [in] json: JSON文本
**          : [in] key: 键名(不含引号)
** 返回		: 值字符串指针, NULL未找到
********************************************************************/
static const char *json_find_value(const char *json, const char *key)
{
    char pattern[24];
    const char *p;

    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) < 0)
        return NULL;
    p = strstr(json, pattern);
    if (p == NULL)
        return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL)
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/*******************************************************************
** 函数名	: json_get_int
** 函数描述	: 取JSON整数字段
** 参数		: [in] json: JSON文本
**          : [in] key: 键名
**          : [out] out: 数值
** 返回		: 0成功, -1未找到或非法
********************************************************************/
static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find_value(json, key);
    char *endp;
    long val;

    if (p == NULL || (*p != '-' && (*p < '0' || *p > '9'))) {
        return -1;
    }
    val = strtol(p, &endp, 10);
    if (endp == p) {
        return -1;
    }
    *out = (int)val;
    return 0;
}

/*******************************************************************
** 函数名	: json_get_str
** 函数描述	: 取JSON字符串字段, 可对引号截取, 不支持转义
** 参数		: [in] json: JSON文本
**          : [in] key: 键名
**          : [out] out: 输出缓冲(含尾'\0')
**          : [in] out_size: 缓冲大小
** 返回		: 0成功, -1未找到或缓冲不足
********************************************************************/
static int json_get_str(const char *json, const char *key, char *out,
                        int out_size)
{
    const char *p = json_find_value(json, key);
    const char *end;
    int len;

    if (p == NULL || *p != '"')
        return -1;
    p++;
    end = strchr(p, '"');
    if (end == NULL)
        return -1;
    len = (int)(end - p);
    if (len + 1 > out_size)
        return -1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    return 0;
}

/*******************************************************************
** 函数名	: ota_check_parse
** 函数描述	: 解析check应答, 提取任务信息到结构体
** 参数		: [in] resp: 缓冲应答体文本(以'\0'结尾)
**          : [out] task: 任务信息输出
** 返回		: 0解析成功(可能无任务tid=-1), -1应答不可解析
********************************************************************/
int ota_check_parse(const char *resp, ota_task_t *task)
{
    int code = ota_resp_code(resp);

    if (code < 0)
        return -1;

    memset(task, 0, sizeof(*task));
    task->code = code;
    task->tid = -1;

    /* 无任务时平台不返回data, tid取不到则-1 */
    json_get_int(resp, "tid", &task->tid);
    json_get_int(resp, "size", &task->size);
    json_get_int(resp, "status", &task->status);
    json_get_int(resp, "type", &task->type);
    json_get_str(resp, "target", task->target, sizeof(task->target));
    json_get_str(resp, "md5", task->md5, sizeof(task->md5));
    
    printf("[ota] check result: code=%d tid=%d size=%d md5=%s\r\n",
           task->code, task->tid, task->size, task->md5);
    
    return 0;
}
