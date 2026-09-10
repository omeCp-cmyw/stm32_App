#ifndef __OTA_MMI_H
#define __OTA_MMI_H

#include <stdint.h>

/*
 * OneNET OTA远程升级状态机（移植自wifi_pro的onenet.c OTA部分）：
 * 链路2双实例异步HTTP客户端，10ms/100ms定时器驱动状态机，
 * 流式写备份分区FW_UPG_WriteData + MD5增量校验。
 *
 * 触发机制：app_gateway云上线边沿触发一次OTA检查，断线重连自动重试。
 * 双向互斥：OTA状态机入口检测FW_UPG_YM_IsArmed中止；
 *          ymodem_arm()检测FW_UPG状态非IDLE（OTA写flash中）拒绝开窗。
 * 升级中禁上报：FW_UPG_GetState() != IDLE时cloud_onenet三处业务上报入口跳过。
 */

/* OTA状态机步骤（详细定义，包含等待状态） */
typedef enum {
    OTA_STEP_IDLE = 0,          /* 空闲，等待触发 */
    OTA_STEP_OPEN,              /* 打开TCP连接 */
    OTA_STEP_OPEN_WAIT,         /* 等待TCP连接结果 */
    OTA_STEP_VERSION,           /* 版本上报 */
    OTA_STEP_VERSION_WAIT,      /* 等待版本上报应答 */
    OTA_STEP_CHECK,             /* 升级检查 */
    OTA_STEP_CHECK_WAIT,        /* 等待升级检查应答 */
    OTA_STEP_DOWNLOAD,          /* 固件下载 */
    OTA_STEP_DOWNLOAD_WAIT,     /* 等待固件下载应答 */
    OTA_STEP_DOWNLOAD_CONTINUE_WAIT, /* 每片下载完成后等待一段时间再继续 */
    OTA_STEP_DOWNLOAD_FAIL_WAIT, /* 下载失败后等待一段时间再重试 */
    OTA_STEP_STATUS_PROGRESS,   /* 上报下载进度(0,10,20,...,90) */
    OTA_STEP_STATUS_PROGRESS_WAIT, /* 等待进度上报应答 */
    OTA_STEP_PROGRESS_FAIL_WAIT, /* 进度上报失败后等待一段时间再继续下载 */
    OTA_STEP_STATUS_100,        /* 上报step 100（升级中） */
    OTA_STEP_STATUS_100_WAIT,   /* 等待step 100应答 */
    OTA_STEP_STATUS_201,        /* 上报step 201（成功） */
    OTA_STEP_STATUS_201_WAIT,   /* 等待step 201应答 */
    OTA_STEP_FINISH,            /* 调用FW_UPG_Finish复位 */
    OTA_STEP_MAX
} ota_step_t;

/*******************************************************************
** 函数名	: ota_init
** 函数描述	: OTA状态机初始化，注册链路2回调
** 参数		: 无
** 返回		: 无
********************************************************************/
void ota_init(void);

/*******************************************************************
** 函数名	: ota_trigger
** 函数描述	: 触发一次OTA检查（云上线边沿调用）
** 参数		: 无
** 返回		: 无
********************************************************************/
void ota_trigger(void);

/*******************************************************************
** 函数名	: ota_get_step
** 函数描述	: 查询当前OTA状态机步骤
** 参数		: 无
** 返回		: ota_step_t步骤
********************************************************************/
ota_step_t ota_get_step(void);

/*******************************************************************
** 函数名	: ota_is_busy
** 函数描述	: 查询OTA是否正在进行（非IDLE）
** 参数		: 无
** 返回		: 1忙, 0空闲
********************************************************************/
uint8_t ota_is_busy(void);

#endif /* __OTA_MMI_H */
