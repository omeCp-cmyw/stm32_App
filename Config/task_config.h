/********************************************************************************
**
** 文件名:     task_config.h
** 版权所有:   无
** 文件描述:   该模块主要实现任务参数配置
**
*********************************************************************************/


#ifndef __TASK_CONFIG_H
#define __TASK_CONFIG_H

#include <stdint.h>

/* 任务优先级定义 */
#define TASK_PRIO_IDLE              0
#define TASK_PRIO_LOW               1
#define TASK_PRIO_NORMAL            2
#define TASK_PRIO_HIGH              3
#define TASK_PRIO_HIGHEST           4
#define TASK_PRIO_MONITOR           5

/* 任务栈大小定义（单位：字，4字节/字） */
#define TASK_STACK_SIZE_SMALL       128     /* 512字节 */
#define TASK_STACK_SIZE_MEDIUM      256     /* 1024字节 */
#define TASK_STACK_SIZE_LARGE       512     /* 2048字节 */
#define TASK_STACK_SIZE_XLARGE      1024    /* 4096字节 */

/* 任务ID定义 */
typedef enum {
    TASK_ID_IDLE = 0,
    TASK_ID_LED,
    TASK_ID_NET_DEBUG,
    TASK_ID_SENSOR,
    TASK_ID_CLOUD,
    TASK_ID_OTA,
    TASK_ID_CAMERA,
    TASK_ID_NTP,
    TASK_ID_LCD,
    TASK_ID_MONITOR,
    TASK_ID_MAX
} task_id_t;

/* 任务信息结构体 */
typedef struct {
    task_id_t id;
    const char *name;
    void (*entry)(void*);
    uint16_t stack_size;
    uint8_t priority;
    uint8_t is_running;
} task_info_t;

#endif /* __TASK_CONFIG_H */
