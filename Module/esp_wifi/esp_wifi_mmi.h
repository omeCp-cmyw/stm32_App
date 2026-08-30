#ifndef __ESP_WIFI_MMI_H
#define __ESP_WIFI_MMI_H

#include "stm32f4xx.h"
#include <stdint.h>
#include "drv_uart_reg.h"

/* WiFi模块串口：USART2 */
#define WIFI_COM    DRV_UART_COM_1

/* WiFi与NTP配置（对应wifi_pro的onenet_config.h） */
#define WIFI_SSID       "123"
#define WIFI_PASS       "yw22334455"
#define WIFI_NTP_SERVER "ntp.aliyun.com"
#define WIFI_NTP_PORT   123

void WIFI_MMI_Init(void);        // WiFi模块初始化：复位+AT指令序列+NTP校时
uint8_t WIFI_MMI_IsReady(void);  // 查询初始化流程是否完成，1完成

#endif /* __ESP_WIFI_MMI_H */
