#include <stdio.h>
#include <string.h>
#include "drv_systick.h"
#include "ntp_mmi.h"

#define NTP_UNIX_DIFF   2208988800u   /* 1900->1970秒差 */
/* 时间范围校验，防止无效应答及2035年后uint32回绕 */
#define NTP_SEC_MIN     3155673600u   /* 2000年 */
#define NTP_SEC_MAX     4260211200u   /* 2035年前 */

static uint32_t s_base_unix;
static uint32_t s_base_ms;
static uint8_t s_valid;

/*******************************************************************
** 函数名	: NtpBe32Read
** 函数描述	: 从缓冲区读取大端32位无符号数
** 参数		: [in] bytes: 指向4字节数据
** 返回		: 大端数值
********************************************************************/
static uint32_t NtpBe32Read(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16)
         | ((uint32_t)bytes[2] << 8)  | (uint32_t)bytes[3];
}

/*******************************************************************
** 函数名	: NTP_MMI_BuildRequest
** 函数描述	: 构建48字节NTP v3客户端请求包
** 参数		: [out] buf: 输出缓冲(至少48字节)
** 返回		: 无
********************************************************************/
void NTP_MMI_BuildRequest(uint8_t *buf)
{
    memset(buf, 0, NTP_MMI_PACKET_SIZE);
    buf[0] = 0x1b;      /* LI=0, VN=3, Mode=3(客户端) */
}

/*******************************************************************
** 函数名	: NTP_MMI_ParseReply
** 函数描述	: 校验并解析NTP应答，提取服务器时间戳
** 参数		: [in] data: 应答数据
**          : [in] len: 数据长度
**          : [out] unix_ts: Unix时间戳
** 返回		: 0合法, -1非法
********************************************************************/
int32_t NTP_MMI_ParseReply(const uint8_t *data, int32_t len, uint32_t *unix_ts)
{
    uint32_t sec;

    if (data == NULL || len < NTP_MMI_PACKET_SIZE)
        return -1;
    if ((data[0] & 0x07) != 4)          /* Mode必须为4(服务器) */
        return -1;
    if (data[1] == 0 || data[1] > 15)   /* 层数检查 */
        return -1;

    sec = NtpBe32Read(data + 40);       /* 服务器发送时间戳 */
    if (sec < NTP_SEC_MIN || sec > NTP_SEC_MAX)
        return -1;

    *unix_ts = sec - NTP_UNIX_DIFF;
    return 0;
}

/*******************************************************************
** 函数名	: NtpDaysToDate
** 函数描述	: 自1970-01-01起的天数换算为年月日(civil_from_days算法)
** 参数		: [in] days: 天数
**          : [out] year: 年
**          : [out] month: 月
**          : [out] day: 日
** 返回		: 无
********************************************************************/
static void NtpDaysToDate(int32_t days, uint16_t *year, uint8_t *month, uint8_t *day)
{
    int32_t z = days + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t d = doy - (153 * mp + 2) / 5 + 1;
    int32_t m = mp < 10 ? mp + 3 : mp - 9;
    int32_t y = yoe + era * 400;

    if (m <= 2)
        y++;
    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
}

/*******************************************************************
** 函数名	: NtpPrintTime
** 函数描述	: 打印某一Unix时间戳的年月日时分秒
** 参数		: [in] tag: 时间制式名(UTC/BEIJING)
**          : [in] unix_ts: Unix时间戳
** 返回		: 无
********************************************************************/
static void NtpPrintTime(const char *tag, uint32_t unix_ts)
{
    uint16_t year;
    uint8_t month, day, hour, min, sec;
    uint32_t secs = unix_ts % 86400;
    int32_t days = (int32_t)(unix_ts / 86400);

    NtpDaysToDate(days, &year, &month, &day);
    hour = (uint8_t)(secs / 3600);
    min  = (uint8_t)((secs % 3600) / 60);
    sec  = (uint8_t)(secs % 60);
    printf("%s: %04u-%02u-%02u %02u:%02u:%02u\r\n",
           tag, year, month, day, hour, min, sec);
}

/*******************************************************************
** 函数名	: NTP_MMI_SetTime
** 函数描述	: 以Unix时间戳建立本地时间基准，并打印UTC/北京时间
** 参数		: [in] unix_ts: Unix时间戳
** 返回		: 无
********************************************************************/
void NTP_MMI_SetTime(uint32_t unix_ts)
{
    s_base_unix = unix_ts;
    s_base_ms = SYSTICK_GetMsTick();
    s_valid = 1;

    NtpPrintTime("ntp time utc    ", unix_ts);
    NtpPrintTime("ntp time beijing", unix_ts + 8 * 3600);
    printf("ntp unix epoch: %lu\r\n", (unsigned long)unix_ts);
}

/*******************************************************************
** 函数名	: NTP_MMI_TimeValid
** 函数描述	: 查询时间是否已同步成功
** 参数		: 无
** 返回		: 1有效, 0未同步
********************************************************************/
uint8_t NTP_MMI_TimeValid(void)
{
    return s_valid;
}

/*******************************************************************
** 函数名	: NTP_MMI_NowUnix
** 函数描述	: 由同步基准+单调时钟推算当前Unix时间
** 参数		: 无
** 返回		: Unix秒, 未同步返回0
********************************************************************/
uint32_t NTP_MMI_NowUnix(void)
{
    if (!s_valid)
        return 0;
    return s_base_unix + (SYSTICK_GetMsTick() - s_base_ms) / 1000;
}

/*******************************************************************
** 函数名	: NTP_MMI_NowMs
** 函数描述	: 当前Unix毫秒时间戳(自同步基准+单调时钟推算)
** 参数		: 无
** 返回		: 毫秒, 未同步返回0
********************************************************************/
uint64_t NTP_MMI_NowMs(void)
{
    if (!s_valid)
        return 0;
    return (uint64_t)s_base_unix * 1000 + (SYSTICK_GetMsTick() - s_base_ms);
}
