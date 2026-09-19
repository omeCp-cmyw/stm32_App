/********************************************************************************
**
** 文件名:     version.h
** 版权所有:   无
** 文件描述:   版本号集中管理（唯一来源，升级版本变更仅修改APP_VERSION）
**
*********************************************************************************/


#ifndef __VERSION_H
#define __VERSION_H

/* 主版本号：软件版本=固件版本=OTA上报版本 */
#define APP_VERSION             "1.0.1"

/* 显示版本（带V前缀，启动日志与cm_backtrace使用） */
#define SOFTWARE_VERSION        "V" APP_VERSION
#define HARDWARE_VERSION        "V1.0.0"
#define SYSTEM_VERSION          "V" APP_VERSION

/* OTA版本（OneNET平台格式，不带V前缀） */
#define APP_OTA_S_VERSION       APP_VERSION    /* 软件版本号(上报) */
#define APP_OTA_F_VERSION       APP_VERSION    /* 固件版本号(升级检查基准) */

#endif /* __VERSION_H */
