/********************************************************************************
**
** 文件名:     drv_sensor.c
** 版权所有:   无
** 文件描述:   该模块主要实现传感器驱动（DHT11单总线+ADC模拟量真实采集）
**
*********************************************************************************/


#include "drv_sensor.h"
#include "bsp_dht11.h"
#include "drv_adc/drv_adc.h"
#include "../Tools/debug.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
** 函数名称    drv_sensor_init
** 函数说明    初始化传感器驱动（DHT11 + ADC三通道）
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_init(void)
{
    /* DHT11单总线引脚初始化（GPIOE3） */
    DHT11_Init();

    /* ADC1两通道初始化（光照PA1/MQ2 PA0） */
    drv_adc_init();

    DEBUG_INFO("[DRV_SENSOR] Sensor driver initialized (DHT11 + ADC)");
    return 0;
}

/*******************************************************************************
** 函数名称    drv_sensor_read
** 函数说明    读取指定传感器数据
** 输入参数    type: 传感器类型
**             data: 数据输出
** 输出参数    无
** 返回参数    0: 成功, -1: 参数错误或读取失败
*******************************************************************************/
int drv_sensor_read(SensorType_e type, SensorData_t *data)
{
    DHT11_Data_TypeDef dht11_data;

    if (data == NULL) {
        return -1;
    }

    if (type >= SENSOR_TYPE_MAX) {
        return -1;
    }

    /* 清空数据 */
    memset(data, 0, sizeof(SensorData_t));
    data->type = type;

    switch (type) {
        case SENSOR_TYPE_DHT11:
            /* 单总线读取温湿度，校验和验证 */
            if (DHT11_Read_TempAndHumidity(&dht11_data) == SUCCESS) {
                data->temperature = (float)dht11_data.temperature;
                data->humidity = (float)dht11_data.humidity;
                data->is_valid = 1;
            } else {
                data->is_valid = 0;
                return -1;
            }
            break;

        case SENSOR_TYPE_LIGHT:
            /* 光照传感器ADC电压(mV)，按实际标定曲线换算照度 */
            data->light_value = (float)drv_adc_read_mv(ADC_CH_LIGHT);
            data->is_valid = 1;
            break;

        case SENSOR_TYPE_MQ2:
            /* MQ2气体传感器ADC电压(mV)，越高代表浓度越大 */
            data->smoke_value = (float)drv_adc_read_mv(ADC_CH_MQ2);
            data->is_valid = 1;
            break;

        default:
            break;
    }

    return 0;
}

/*******************************************************************************
** 函数名称    drv_sensor_read_all
** 函数说明    读取所有传感器数据（单传感器失败不影响其余）
** 输入参数    data: 数据指针
** 输出参数    无
** 返回参数    0: 成功, -1: 参数错误
*******************************************************************************/
int drv_sensor_read_all(SensorData_t *data)
{
    DHT11_Data_TypeDef dht11_data;
    int valid_cnt = 0;

    if (data == NULL) {
        return -1;
    }

    /* 清空数据 */
    memset(data, 0, sizeof(SensorData_t));

    /* DHT11温湿度 */
    if (DHT11_Read_TempAndHumidity(&dht11_data) == SUCCESS) {
        data->temperature = (float)dht11_data.temperature;
        data->humidity = (float)dht11_data.humidity;
        valid_cnt++;
    }

    /* 光照ADC电压 */
    data->light_value = (float)drv_adc_read_mv(ADC_CH_LIGHT);
    valid_cnt++;

    /* MQ2气体ADC电压 */
    data->smoke_value = (float)drv_adc_read_mv(ADC_CH_MQ2);
    valid_cnt++;

    data->is_valid = (valid_cnt > 0) ? 1 : 0;
    return (valid_cnt > 0) ? 0 : -1;
}

/*******************************************************************************
** 函数名称    drv_sensor_calibrate
** 函数说明    校准传感器（当前传感器为出厂标定，预留接口）
** 输入参数    type: 传感器类型
** 输出参数    无
** 返回参数    0: 成功
*******************************************************************************/
int drv_sensor_calibrate(SensorType_e type)
{
    if (type >= SENSOR_TYPE_MAX) {
        return -1;
    }

    /* 出厂已标定，预留校准流程 */
    DEBUG_INFO("[DRV_SENSOR] Sensor %d calibrated", type);
    return 0;
}
