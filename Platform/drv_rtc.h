/********************************************************************************
**
** 文件名:     drv_rtc.h
** 版权所有:   无
** 文件描述:   RTC实时时钟驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_RTC_H
#define __DRV_RTC_H

#include <stdint.h>

int drv_rtc_init(void);
uint8_t drv_rtc_is_valid(void);
int drv_rtc_set_unix(uint32_t unix_ts);
int drv_rtc_get_str(char *buf, uint16_t len);

#endif /* __DRV_RTC_H */
