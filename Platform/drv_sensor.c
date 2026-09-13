/********************************************************************************
**
** 文件名:     drv_sensor.c
** 版权所有:   无
** 文件描述:   该模块主要实现传感器驱动
**
*********************************************************************************/


#include "drv_sensor.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
** 函数名称    drv_sensor_init
** 函数说明    初始化传感器驱动
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_init(void)
{
    printf("[DRV_SENSOR] Sensor driver initialized\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_sensor_read
** 函数说明    读取传感器数据
** 输入参数    type: 传感器类型
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_read(SensorType_e type, SensorData_t *data)
{
    if (data == NULL) {
        return -1;
    }
    
    if (type >= SENSOR_TYPE_MAX) {
        return -1;
    }
    
    /* 清空数据 */
    memset(data, 0, sizeof(SensorData_t));
    data->type = type;
    data->is_valid = 1;
    
    switch (type) {
        case SENSOR_TYPE_DHT11:
            data->temperature = 25.0f;
            data->humidity = 50.0f;
            break;
        case SENSOR_TYPE_LIGHT:
            data->light_value = 500.0f;
            break;
        case SENSOR_TYPE_SMOKE:
        case SENSOR_TYPE_MQ2:
            data->smoke_value = 100.0f;
            break;
        default:
            break;
    }
    
    printf("[DRV_SENSOR] Read sensor %d\r\n", type);
    return 0;
}

/*******************************************************************************
** 函数名称    drv_sensor_read_all
** 函数说明    读取所有传感器数据
** 输入参数    data: 数据指针
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_read_all(SensorData_t *data)
{
    if (data == NULL) {
        return -1;
    }
    
    /* 清空数据 */
    memset(data, 0, sizeof(SensorData_t));
    data->type = SENSOR_TYPE_DHT11;
    data->temperature = 25.0f;
    data->humidity = 50.0f;
    data->light_value = 500.0f;
    data->smoke_value = 100.0f;
    data->is_valid = 1;
    
    printf("[DRV_SENSOR] Read all sensors\r\n");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_sensor_calibrate
** 函数说明    校准传感器
** 输入参数    type: 传感器类型
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_calibrate(SensorType_e type)
{
    if (type >= SENSOR_TYPE_MAX) {
        return -1;
    }
    
    printf("[DRV_SENSOR] Calibrate sensor %d\r\n", type);
    return 0;
}
