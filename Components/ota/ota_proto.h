/********************************************************************************
**
** 文件名:     ota_proto.h
** 版权所有:   无
** 文件描述:   OneNET OTA远程升级协议层接口定义（HTTP请求拼装与应答解析）
**             移植自 STM32F4\stm32_App 工程 Components\cloud\ota\ota_proto
**
*********************************************************************************/

#ifndef __OTA_PROTO_H
#define __OTA_PROTO_H

#include <stdint.h>

/* OTA任务信息（check应答解析结果） */
typedef struct {
    int code;           /* 应答code, 0成功 */
    int tid;            /* 任务ID, -1表示无任务 */
    int size;           /* 固件字节数 */
    int status;         /* 1进行中 2已完成 3已取消 */
    int type;           /* 1整包 2分包 */
    char target[32];    /* 目标版本 */
    char md5[40];       /* 固件md5 */
} ota_task_t;

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
                        char *out, int out_size);

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
                      const char *version, char *out, int out_size);

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
                         char *out, int out_size);

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
                       int tid, int step, char *out, int out_size);

/*******************************************************************
** 函数名	: ota_resp_code
** 函数描述	: 从HTTP应答体中提取code字段值, OTA接口通用
** 参数		: [in] resp: 缓冲应答体文本(以'\0'结尾)
** 返回		: code值, -1未找到或解析失败
********************************************************************/
int ota_resp_code(const char *resp);

/*******************************************************************
** 函数名	: ota_check_parse
** 函数描述	: 解析check应答, 提取任务信息到结构体
** 参数		: [in] resp: 缓冲应答体文本(以'\0'结尾)
**          : [out] task: 任务信息输出
** 返回		: 0解析成功(可能无任务tid=-1), -1应答不可解析
********************************************************************/
int ota_check_parse(const char *resp, ota_task_t *task);

#endif /* __OTA_PROTO_H */
