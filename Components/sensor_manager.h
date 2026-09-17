/********************************************************************************
**
** 文件名:     sensor_manager.h
** 版权所有:   无
** 文件描述:   该模块主要实现传感器数据采集管理接口定义
**
*********************************************************************************/


#ifndef __SENSOR_MANAGER_H
#define __SENSOR_MANAGER_H

#include <stdint.h>
#include "../Platform/drv_sensor.h"

/* 传感器管理器配置 */
typedef struct {
    uint32_t collect_period;                    // 采集周期(ms)
    uint8_t sensor_enable[SENSOR_TYPE_MAX];     // 传感器使能标志
} SensorManager_Config_t;

/* 传感器管理器接口 */
int sensor_manager_init(void);
int sensor_manager_deinit(void);
int sensor_manager_start_collect(void);
int sensor_manager_stop_collect(void);
int sensor_manager_get_data(SensorData_t *data);
/**
  * @brief  获取最新采集数据（只读缓存，不触发采集）
  * @param  data: 数据指针
  * @retval 0: 成功, -1: 参数错误, -2: 暂无有效数据
  */
int sensor_manager_get_latest_data(SensorData_t *data);
int sensor_manager_set_collect_period(uint32_t period);
int sensor_manager_enable_sensor(SensorType_e type, uint8_t enable);

#endif /* __SENSOR_MANAGER_H */
