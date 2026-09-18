/********************************************************************************
**
** 文件名:     drv_rtc.c
** 版权所有:   无
** 文件描述:   RTC实时时钟驱动
**             LSE优先，启动超时自动降级LSI；
**             备份寄存器DR0存初始化标志、DR1存时间有效标志
**
*********************************************************************************/


#include <stdio.h>
#include "drv_rtc.h"
#include "stm32f4xx_hal.h"

/* 备份寄存器标志值 */
#define RTC_BKP_DATA            0x32F2

static RTC_HandleTypeDef hrtc;

/* LSE启动超时次数：使用stm32f4xx_hal_conf.h中的LSE_STARTUP_TIMEOUT */

/*******************************************************************************
** 函数名称    RtcWeekDayCalc
** 函数说明    蔡勒公式计算星期（HAL约定：1=周一 ... 7=周日）
** 输入参数    year: 年(2000~2099)
**             month: 月(1~12)
**             day: 日(1~31)
** 输出参数    无
** 返回参数    星期值(1~7)
*******************************************************************************/
static uint8_t RtcWeekDayCalc(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t h;

    if (month < 3) {
        month += 12;
        year--;
    }
    /* 蔡勒公式：0=周六 1=周日 2=周一 ... 6=周五 */
    h = (day + 13 * (month + 1) / 5 + year + year / 4 - year / 100 + year / 400) % 7;
    /* 换算为HAL约定：1=周一 ... 7=周日 */
    return (uint8_t)((h + 5) % 7 + 1);
}

/*******************************************************************************
** 函数名称    RtcDaysToDate
** 函数说明    自1970-01-01起的天数换算为年月日(civil_from_days算法)
** 输入参数    days: 天数
** 输出参数    year: 年
**             month: 月
**             day: 日
** 返回参数    无
*******************************************************************************/
static void RtcDaysToDate(int32_t days, uint16_t *year, uint8_t *month, uint8_t *day)
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

    if (m <= 2) {
        y++;
    }
    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
}

/*******************************************************************************
** 函数名称    drv_rtc_init
** 函数说明    初始化RTC：LSE优先超时降级LSI，配置1Hz分频
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int drv_rtc_init(void)
{
    __IO uint16_t startup = 0;
    FlagStatus lse_status = RESET;

    /* 使能PWR时钟与备份域访问 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    hrtc.Instance = RTC;

    /* 已初始化（备份域保持），直接返回避免丢时间 */
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BKP_DATA) {
        printf("[DRV_RTC] RTC already initialized, keep time\r\n");
        return 0;
    }

    /* 首次初始化：复位备份域 */
    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();

    /* LSE优先，超时降级LSI */
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    do {
        lse_status = (FlagStatus)__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY);
        startup++;
    } while ((lse_status == RESET) && (startup != LSE_STARTUP_TIMEOUT));

    if (lse_status == SET) {
        printf("[DRV_RTC] LSE startup success\r\n");
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    } else {
        printf("[DRV_RTC] LSE fail, fallback to LSI\r\n");
        __HAL_RCC_LSI_ENABLE();
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
        }
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
    }

    __HAL_RCC_RTC_ENABLE();

    /* 1Hz分频：32768/[(127+1)*(255+1)] */
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 0x7F;
    hrtc.Init.SynchPrediv = 0xFF;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        printf("[DRV_RTC] RTC init fail!\r\n");
        return -1;
    }

    /* 写初始化标志，下次上电跳过 */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BKP_DATA);
    printf("[DRV_RTC] RTC initialized\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_rtc_is_valid
** 函数说明    查询RTC时间是否已被设置过
** 输入参数    无
** 输出参数    无
** 返回参数    1: 有效, 0: 未设置
*******************************************************************************/
uint8_t drv_rtc_is_valid(void)
{
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) == RTC_BKP_DATA) ? 1 : 0;
}

/*******************************************************************************
** 函数名称    drv_rtc_set_unix
** 函数说明    以Unix时间戳设置RTC（调用方负责时区换算）
** 输入参数    unix_ts: Unix时间戳（秒）
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int drv_rtc_set_unix(uint32_t unix_ts)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    uint16_t year;
    uint8_t month, day;
    uint32_t secs = unix_ts % 86400;
    int32_t days = (int32_t)(unix_ts / 86400);

    RtcDaysToDate(days, &year, &month, &day);
    if (year < 2000 || year > 2099) {
        return -1;
    }

    sTime.Hours = (uint8_t)(secs / 3600);
    sTime.Minutes = (uint8_t)((secs % 3600) / 60);
    sTime.Seconds = (uint8_t)(secs % 60);
    sTime.SubSeconds = 0;
    sTime.TimeFormat = RTC_HOURFORMAT12_AM;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    sDate.WeekDay = RtcWeekDayCalc(year, month, day);
    sDate.Month = month;
    sDate.Date = day;
    sDate.Year = (uint8_t)(year - 2000);

    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        return -1;
    }
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        return -1;
    }

    /* 写时间有效标志，掉电重启后时间仍可信 */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, RTC_BKP_DATA);
    printf("[DRV_RTC] time set: %04u-%02u-%02u %02u:%02u:%02u\r\n",
           year, month, day, sTime.Hours, sTime.Minutes, sTime.Seconds);
    return 0;
}

/*******************************************************************************
** 函数名称    drv_rtc_get_str
** 函数说明    读取RTC当前时间并格式化为字符串
** 输入参数    buf: 输出缓冲(至少20字节)
**             len: 缓冲长度
** 输出参数    无
** 返回参数    0: 成功, -1: 失败
*******************************************************************************/
int drv_rtc_get_str(char *buf, uint16_t len)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    if (buf == NULL || len < 20) {
        return -1;
    }
    if (!drv_rtc_is_valid()) {
        return -1;
    }

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
             2000u + sDate.Year, sDate.Month, sDate.Date,
             sTime.Hours, sTime.Minutes, sTime.Seconds);
    return 0;
}
