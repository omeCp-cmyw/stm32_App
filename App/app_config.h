/********************************************************************************
**
** 文件名:     app_config.h
** 版权所有:   无
** 文件描述:   该模块主要实现应用层参数配置
**
*********************************************************************************/


#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include <stdint.h>

/* 应用任务使能开关 */
#define APP_ENABLE_LED              1   /* 使能LED闪烁任务 */
#define APP_ENABLE_NET_DEBUG        0   /* 使能网络调试任务 */
#define APP_ENABLE_MONITOR          1   /* 使能系统监控任务 */

/* 第二阶段任务使能开关（组件层已实现，传感器与云平台默认开启） */
#define APP_ENABLE_SENSOR           1   /* 使能传感器采集任务 */
#define APP_ENABLE_CLOUD            1   /* 使能云平台通信任务 */
#define APP_ENABLE_CAMERA           0   /* 使能摄像头任务 */
#define APP_ENABLE_LCD              1   /* 使能LCD显示任务 */
#define APP_ENABLE_NTP              0   /* 使能NTP时间同步任务 */
#define APP_ENABLE_OTA              0   /* 使能OTA升级任务 */
#define APP_ENABLE_YMODEM           1   /* 使能Ymodem本地升级（UART3通道，与OTA分离） */

/* LED闪烁任务配置 */
#define APP_LED_TOGGLE_PERIOD_MS    500     /* LED翻转周期(ms) */

/* 网络调试任务配置 */
#define APP_NET_DEBUG_WELCOME       "STM32 IoT Terminal Connected!\r\n"    /* 欢迎消息 */
#define APP_NET_DEBUG_HEARTBEAT     "HEARTBEAT\r\n"                        /* 心跳消息 */
#define APP_NET_DEBUG_RECV_TIMEOUT  100         /* 接收超时(ms) */
#define APP_NET_DEBUG_RECONNECT_MS  2000        /* 断线重连间隔(ms) */
#define APP_NET_DEBUG_HEARTBEAT_MS  10000       /* 心跳发送间隔(ms) */

/* 系统监控任务配置 */
#define APP_MONITOR_PERIOD_MS       10000       /* 监控打印周期(ms) */

/* 传感器采集任务配置 */
#define APP_SENSOR_COLLECT_PERIOD_MS 10000      /* 传感器采集周期(ms) */

/* 云平台通信任务配置 */
#define APP_CLOUD_STATUS_PERIOD_MS  1000        /* 云状态监控周期(ms) */

#endif /* __APP_CONFIG_H */
