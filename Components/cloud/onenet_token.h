#ifndef __ONENET_TOKEN_H
#define __ONENET_TOKEN_H

#include <stdint.h>

/* 生成的token最大长度(含结尾'\0') */
#define ONENET_TOKEN_MAX_LEN    200

/*******************************************************************
** 函数名	: onenet_token_build
** 函数描述	: 生成OneNET设备鉴权token: 产品访问密钥做HMAC-MD5
**			密钥，签名字符串et/method/res/version换行分隔，
**			sign经base64并URL转义后与参数拼接
** 参数		: [in] expire_ts: 过期时间戳(unix秒)
**          : [in] product_id: 产品ID
**          : [in] device_name: 设备名
**          : [in] access_key: 产品访问密钥(base64文本)
**          : [out] token: token输出缓冲
**          : [in] token_size: 缓冲大小(不小于ONENET_TOKEN_MAX_LEN)
** 返回		: 0成功, -1失败
********************************************************************/
int onenet_token_build(uint32_t expire_ts, const char *product_id,
                       const char *device_name, const char *access_key,
                       char *token, int token_size);

#endif /* __ONENET_TOKEN_H */
