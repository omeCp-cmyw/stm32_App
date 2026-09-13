/********************************************************************************
**
** 文件名:     sensor_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现传感器数据采集管理
**
*********************************************************************************/


#include "sensor_manager.h"
#include <stdio.h>
#include <string.h>

/* 传感器管理器配置（桩函数，未来使用） */
static SensorManager_Config_t sensor_config = {
    .collect_period = 1000,
    .sensor_enable = {1, 1, 1, 1}
};

/* 传感器数据缓存（桩函数） */
static SensorData_t sensor_data_cache;

/*******************************************************************************
** 函数名称    sensor_manager_init
** 函数说明    初始化传感器管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_init(void)
{
    (void)sensor_config;  /* 消除未使用变量警告，未来实现时使用 */
    memset(&sensor_data_cache, 0, sizeof(sensor_data_cache));
    
    printf("[SENSOR_MANAGER] Sensor manager initialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_deinit
** 函数说明    反初始化传感器管理器（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_deinit(void)
{
    printf("[SENSOR_MANAGER] Sensor manager deinitialized (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_start_collect
** 函数说明    开始采集（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_start_collect(void)
{
    printf("[SENSOR_MANAGER] Start collecting (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_stop_collect
** 函数说明    停止采集（桩函数）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_stop_collect(void)
{
    printf("[SENSOR_MANAGER] Stop collecting (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_get_data
** 函数说明    获取传感器数据（桩函数）
** 输入参数    data: 数据指针
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_get_data(SensorData_t *data)
{
    if (data == NULL) {
        return -1;
    }
    
    /* 返回桩数据 */
    *data = sensor_data_cache;
    data->temperature = 25.0f;
    data->humidity = 50.0f;
    data->light_value = 500.0f;
    data->smoke_value = 100.0f;
    data->is_valid = 1;
    
    printf("[SENSOR_MANAGER] Get sensor data (stub)\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_set_collect_period
** 函数说明    设置采集周期（桩函数）
** 输入参数    period: 采集周期(ms)
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_set_collect_period(uint32_t period)
{
    sensor_config.collect_period = period;
    printf("[SENSOR_MANAGER] Set collect period to %d ms (stub)\r\n", (int)period);
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_enable_sensor
** 函数说明    使能/禁用传感器（桩函数）
** 输入参数    type: 传感器类型
**             enable: 使能标志
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_enable_sensor(SensorType_e type, uint8_t enable)
{
    if (type >= SENSOR_TYPE_MAX) {
        return -1;
    }
    
    sensor_config.sensor_enable[type] = enable;
    printf("[SENSOR_MANAGER] Sensor %d %s (stub)\r\n", type, enable ? "enabled" : "disabled");
    return 0;
}
