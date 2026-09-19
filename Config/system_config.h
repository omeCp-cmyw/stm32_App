/********************************************************************************
**
** 文件名:     system_config.h
** 版权所有:   无
** 文件描述:   该模块主要实现系统参数配置
**
*********************************************************************************/


#ifndef __SYSTEM_CONFIG_H
#define __SYSTEM_CONFIG_H

#include <stdint.h>
#include "version.h"

/* APP运行地址配置 */
#define APP_START_ADDR              0x08020000
#define APP_END_ADDR                0x0807FFFF
#define APP_MAX_SIZE                (APP_END_ADDR - APP_START_ADDR + 1)  /* 384KB */
#define NVIC_VETOR_TABLE_OFFSET    (APP_START_ADDR - 0x08000000)        /* 0x20000 */

/* Flash分区地址定义 */
#define BOOTLOADER_ADDR             0x08000000
#define OTA_BACKUP_ADDR             0x08080000
#define OTA_BACKUP_SIZE             (256 * 1024)  /* 256KB */
#define CONFIG_STORAGE_ADDR         0x080C0000
#define CONFIG_STORAGE_SIZE         (256 * 1024)  /* 256KB */

/* 网络配置（与上位机同网段） */
#define LOCAL_IP_ADDR0              192
#define LOCAL_IP_ADDR1              168
#define LOCAL_IP_ADDR2              1
#define LOCAL_IP_ADDR3              100

#define LOCAL_NETMASK0              255
#define LOCAL_NETMASK1              255
#define LOCAL_NETMASK2              255
#define LOCAL_NETMASK3              0

#define LOCAL_GW0                   192
#define LOCAL_GW1                   168
#define LOCAL_GW2                   1
#define LOCAL_GW3                   1

/* 调试服务器配置（上位机） */
#define DEBUG_SERVER_IP0            192
#define DEBUG_SERVER_IP1            168
#define DEBUG_SERVER_IP2            1
#define DEBUG_SERVER_IP3            12

#define DEBUG_SERVER_PORT           8080

/* 调试串口配置 */
#define DEBUG_USART_BAUDRATE        115200

/* 系统功能开关 */
#define ENABLE_CLOUD                1   /* 使能云平台功能 */
#define ENABLE_OTA                  1   /* 使能OTA升级功能 */
#define ENABLE_YMODEM               1   /* 使能Ymodem升级功能 */
#define ENABLE_CAMERA               1   /* 使能摄像头功能 */
#define ENABLE_LCD                  1   /* 使能LCD显示功能 */
#define ENABLE_NTP                  1   /* 使能NTP时间同步 */

#endif /* __SYSTEM_CONFIG_H */
