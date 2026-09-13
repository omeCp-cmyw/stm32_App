/********************************************************************************
**
** 文件名:     ota_manager.h
** 版权所有:   无
** 文件描述:   该模块主要实现OTA升级管理接口定义
**
*********************************************************************************/


#ifndef __OTA_MANAGER_H
#define __OTA_MANAGER_H

#include <stdint.h>

/* OTA状态定义 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECK,
    OTA_STATE_DOWNLOAD,
    OTA_STATE_VERIFY,
    OTA_STATE_APPLY,
    OTA_STATE_MAX
} OTA_State_e;

/* OTA错误码定义 */
typedef enum {
    OTA_OK = 0,
    OTA_ERR_NO_UPDATE,
    OTA_ERR_DOWNLOAD,
    OTA_ERR_VERIFY,
    OTA_ERR_FLASH,
    OTA_ERR_MAX
} OTA_Error_e;

/* OTA信息结构 */
typedef struct {
    OTA_State_e state;
    OTA_Error_e last_error;
    uint32_t firmware_size;
    uint32_t downloaded_size;
    uint32_t firmware_crc;
    uint32_t progress;          // 进度(0-100)
} OTA_Info_t;

/* OTA管理器接口 */
int ota_manager_init(void);
int ota_manager_deinit(void);
int ota_manager_check_update(void);
int ota_manager_start_download(void);
int ota_manager_get_info(OTA_Info_t *info);
int ota_manager_get_progress(void);
int ota_manager_apply_update(void);
int ota_manager_cancel(void);

#endif /* __OTA_MANAGER_H */
