/********************************************************************************
**
** 文件名:     drv_sensor.h
** 版权所有:   无
** 文件描述:   该模块主要实现传感器驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_SENSOR_H
#define __DRV_SENSOR_H

#include <stdint.h>

/* 传感器类型 */
typedef enum {
    SENSOR_TYPE_DHT11 = 0,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_MQ2,
    SENSOR_TYPE_MAX
} SensorType_e;

/* 传感器数据 */
typedef struct {
    SensorType_e type;
    float temperature;
    float humidity;
    float light_value;      /* 环境光强度(lux, AP3216C ALS) */
    uint16_t ps_data;       /* 接近感应(AP3216C PS, bit15:1近0远) */
    uint16_t ir_data;       /* 红外强度(AP3216C IR) */
    float smoke_value;      /* MQ2气体ADC电压(mV) */
    uint32_t timestamp;
    uint8_t is_valid;
} SensorData_t;

/* 传感器驱动接口 */
int drv_sensor_init(void);
int drv_sensor_read(SensorType_e type, SensorData_t *data);
int drv_sensor_read_all(SensorData_t *data);
int drv_sensor_calibrate(SensorType_e type);

#endif /* __DRV_SENSOR_H */
