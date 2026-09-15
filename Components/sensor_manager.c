/********************************************************************************
**
** 文件名:     sensor_manager.c
** 版权所有:   无
** 文件描述:   该模块主要实现传感器数据采集管理
**
*********************************************************************************/


#include "sensor_manager.h"
#include "../Tools/debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* 传感器管理器配置 */
static SensorManager_Config_t sensor_config = {
    .collect_period = 5000,
    .sensor_enable = {1, 1, 1}
};

/* 采集运行标志 */
static uint8_t collect_running = 0;

/*******************************************************************************
** 函数名称    sensor_manager_init
** 函数说明    初始化传感器管理器（初始化底层驱动）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_init(void)
{
    collect_running = 0;

    if (drv_sensor_init() != 0) {
        DEBUG_ERROR("[SENSOR_MANAGER] Driver init failed");
        return -1;
    }

    DEBUG_INFO("[SENSOR_MANAGER] Sensor manager initialized");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_deinit
** 函数说明    反初始化传感器管理器（停止采集）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_deinit(void)
{
    collect_running = 0;

    DEBUG_INFO("[SENSOR_MANAGER] Sensor manager deinitialized");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_start_collect
** 函数说明    开始采集（置运行标志）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_start_collect(void)
{
    collect_running = 1;

    DEBUG_INFO("[SENSOR_MANAGER] Collect started, period %d ms",
               (int)sensor_config.collect_period);
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_stop_collect
** 函数说明    停止采集（清运行标志）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_stop_collect(void)
{
    collect_running = 0;

    DEBUG_INFO("[SENSOR_MANAGER] Collect stopped");
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_get_data
** 函数说明    获取传感器数据（真实采集，按使能掩码过滤）
** 输入参数    data: 数据指针
** 输出参数    无
** 返回参数    0: 成功, -1: 未运行或参数错误, -2: 采集失败
*******************************************************************************/
int sensor_manager_get_data(SensorData_t *data)
{
    SensorData_t all_data;

    if (data == NULL) {
        return -1;
    }

    if (!collect_running) {
        return -1;
    }

    /* 底层真实采集 */
    if (drv_sensor_read_all(&all_data) != 0) {
        return -2;
    }

    memset(data, 0, sizeof(SensorData_t));

    /* 按使能掩码填充数据 */
    if (sensor_config.sensor_enable[SENSOR_TYPE_DHT11]) {
        data->temperature = all_data.temperature;
        data->humidity = all_data.humidity;
    }
    if (sensor_config.sensor_enable[SENSOR_TYPE_LIGHT]) {
        data->light_value = all_data.light_value;
        data->ps_data = all_data.ps_data;
        data->ir_data = all_data.ir_data;
    }
    if (sensor_config.sensor_enable[SENSOR_TYPE_MQ2]) {
        data->smoke_value = all_data.smoke_value;
    }

    data->timestamp = xTaskGetTickCount();
    data->is_valid = all_data.is_valid;
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_set_collect_period
** 函数说明    设置采集周期（由采集任务按新周期调度）
** 输入参数    period: 采集周期(ms)
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int sensor_manager_set_collect_period(uint32_t period)
{
    if (period == 0) {
        return -1;
    }

    sensor_config.collect_period = period;
    DEBUG_INFO("[SENSOR_MANAGER] Collect period set to %d ms", (int)period);
    return 0;
}

/*******************************************************************************
** 函数名称    sensor_manager_enable_sensor
** 函数说明    使能/禁用传感器
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

    sensor_config.sensor_enable[type] = enable ? 1 : 0;
    DEBUG_INFO("[SENSOR_MANAGER] Sensor %d %s", type, enable ? "enabled" : "disabled");
    return 0;
}
