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
    SENSOR_TYPE_SMOKE,
    SENSOR_TYPE_MQ2,
    SENSOR_TYPE_MAX
} SensorType_e;

/* 传感器数据 */
typedef struct {
    SensorType_e type;
    float temperature;
    float humidity;
    float light_value;
    float smoke_value;
    uint32_t timestamp;
    uint8_t is_valid;
} SensorData_t;

/* 传感器驱动接口 */
int drv_sensor_init(void);
int drv_sensor_read(SensorType_e type, SensorData_t *data);
int drv_sensor_read_all(SensorData_t *data);
int drv_sensor_calibrate(SensorType_e type);

#endif /* __DRV_SENSOR_H */
