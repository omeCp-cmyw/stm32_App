/********************************************************************************
**
** 文件名:     md5.c
** 版权所有:   无
** 文件描述:   RFC1321 MD5消息摘要算法实现（64步4轮变换，小端字节序，
**            与OpenSSL结果一致，供OneNET token HMAC-MD5鉴权使用）
**
*********************************************************************************/

#include <string.h>
#include "md5.h"

/* 正弦表常量 */
static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

/* 每轮循环左移位数 */
static const uint8_t S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,
};

#define F(x, y, z)  (((x) & (y)) | ((~(x)) & (z)))
#define G(x, y, z)  (((x) & (z)) | ((y) & (~(z))))
#define H(x, y, z)  ((x) ^ (y) ^ (z))
#define I(x, y, z)  ((y) ^ ((x) | (~(z))))
#define ROTL(x, n)  (((x) << (n)) | ((x) >> (32 - (n))))

/*******************************************************************************
** 函数名称    md5_transform
** 函数说明    64字节块变换，更新链接变量
** 输入参数    ctx: 上下文
**             block: 64字节数据块
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void md5_transform(MD5_CTX *ctx, const uint8_t *block)
{
    uint32_t X[16];
    uint32_t a = ctx->A, b = ctx->B, c = ctx->C, d = ctx->D;
    uint32_t t;
    int i;

    /* 小端读入16个字 */
    for (i = 0; i < 16; i++) {
        X[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);
    }

    /* 4轮共64步 */
    for (i = 0; i < 64; i++) {
        int g;

        if (i < 16) {
            t = F(b, c, d);
            g = i;
        } else if (i < 32) {
            t = G(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            t = H(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            t = I(b, c, d);
            g = (7 * i) % 16;
        }
        t = t + a + K[i] + X[g];
        a = d;
        d = c;
        c = b;
        b = b + ROTL(t, S[i]);
    }

    ctx->A += a;
    ctx->B += b;
    ctx->C += c;
    ctx->D += d;
}

/*******************************************************************************
** 函数名称    MD5_Init
** 函数说明    初始化MD5上下文
** 输入参数    c: 上下文
** 输出参数    无
** 返回参数    1: 成功
*******************************************************************************/
int MD5_Init(MD5_CTX *c)
{
    memset(c, 0, sizeof(*c));
    c->A = 0x67452301;
    c->B = 0xefcdab89;
    c->C = 0x98badcfe;
    c->D = 0x10325476;
    return 1;
}

/*******************************************************************************
** 函数名称    MD5_Update
** 函数说明    追加摘要数据，累计位长度并按64字节块变换
** 输入参数    c: 上下文
**             data: 数据
**             len: 数据长度
** 输出参数    无
** 返回参数    1: 成功
*******************************************************************************/
int MD5_Update(MD5_CTX *c, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t nl, part;

    /* 累计位长度 */
    nl = c->Nl + (len << 3);
    if (nl < c->Nl) {
        c->Nh++;
    }
    c->Nh += (len >> 29);
    c->Nl = nl;

    /* 补齐现有半块 */
    part = MD5_CBLOCK - c->num;
    if (len < part) {
        memcpy((uint8_t *)c->data + c->num, p, len);
        c->num += len;
        return 1;
    }
    memcpy((uint8_t *)c->data + c->num, p, part);
    md5_transform(c, (const uint8_t *)c->data);
    p += part;
    len -= part;

    /* 整块变换 */
    while (len >= MD5_CBLOCK) {
        md5_transform(c, p);
        p += MD5_CBLOCK;
        len -= MD5_CBLOCK;
    }

    /* 剩余存缓冲 */
    memcpy((uint8_t *)c->data, p, len);
    c->num = len;
    return 1;
}

/*******************************************************************************
** 函数名称    MD5_Final
** 函数说明    追加填充与位长度完成计算，输出16字节小端摘要
** 输入参数    c: 上下文
** 输出参数    md: 摘要缓冲(16字节)
** 返回参数    1: 成功
*******************************************************************************/
int MD5_Final(uint8_t *md, MD5_CTX *c)
{
    uint8_t tail[72];
    uint64_t bitlen = ((uint64_t)c->Nh << 32) | c->Nl;
    uint32_t num = c->num % MD5_CBLOCK;
    uint32_t pad;   /* 0x80+0填充+8字节位长度的总追加数 */
    int i;

    /* 填充后总长对齐到块边界: pad = 56-num, num>=56时加一整个块 */
    pad = (num < MD5_CBLOCK - 8) ?
          (MD5_CBLOCK - 8 - num) :
          (MD5_CBLOCK + MD5_CBLOCK - 8 - num);

    memset(tail, 0, sizeof(tail));
    tail[0] = 0x80;
    for (i = 0; i < 8; i++) {
        tail[pad + i] = (uint8_t)(bitlen >> (8 * i));
    }
    MD5_Update(c, tail, pad + 8);

    /* 小端输出 */
    for (i = 0; i < 4; i++) {
        md[i]      = (uint8_t)(c->A >> (8 * i));
        md[i + 4]  = (uint8_t)(c->B >> (8 * i));
        md[i + 8]  = (uint8_t)(c->C >> (8 * i));
        md[i + 12] = (uint8_t)(c->D >> (8 * i));
    }
    memset(c, 0, sizeof(*c));
    return 1;
}

/*******************************************************************************
** 函数名称    MD5
** 函数说明    一次性计算数据MD5摘要
** 输入参数    d: 数据
**             n: 数据长度
** 输出参数    md: 摘要缓冲(16字节)，NULL时用内部静态缓冲
** 返回参数    摘要指针
*******************************************************************************/
uint8_t *MD5(const uint8_t *d, uint32_t n, uint8_t *md)
{
    static uint8_t m[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;

    if (md == 0) {
        md = m;
    }
    MD5_Init(&ctx);
    MD5_Update(&ctx, d, n);
    MD5_Final(md, &ctx);
    return md;
}
