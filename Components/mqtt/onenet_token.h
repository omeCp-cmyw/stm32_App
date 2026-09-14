/********************************************************************************
**
** 文件名:     onenet_token.h
** 版权所有:   无
** 文件描述:   OneNET设备鉴权token生成接口定义
**
*********************************************************************************/

#ifndef __ONENET_TOKEN_H
#define __ONENET_TOKEN_H

#include <stdint.h>

/* 生成的token最大长度(含结尾'\0') */
#define ONENET_TOKEN_MAX_LEN    200

/*******************************************************************************
** 函数名称    onenet_token_build
** 函数说明    生成OneNET设备鉴权token: 产品访问密钥做HMAC-MD5密钥，
**             签名字符串et/method/res/version换行分隔，
**             sign经base64并URL转义后与参数拼接
** 输入参数    expire_ts: 过期时间戳(unix秒)
**             product_id: 产品ID
**             device_name: 设备名
**             access_key: 产品访问密钥(base64文本)
** 输出参数    token: token输出缓冲
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int onenet_token_build(uint32_t expire_ts, const char *product_id,
                       const char *device_name, const char *access_key,
                       char *token, int token_size);

#endif /* __ONENET_TOKEN_H */
