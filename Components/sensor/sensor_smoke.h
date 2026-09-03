#ifndef SENSOR_SMOKE_H
#define SENSOR_SMOKE_H

#include "sensor_core.h"

/*
 * 烟雾传感器组件：MQ-2/MQ-135类气敏传感器ADC采集。
 * 传感器层只输出原始电压值smoke_value(mV)；
 * 告警判断(迟滞+防抖)由App物模型规则层(app_devmodel.c)完成，
 * 与现有温湿度越限模式一致。阈值/防抖配置集中在本文件顶部。
 */

/* 配置宏(实测后按传感器特性标定调整) */
#define SMOKE_ALARM_MV      1800    /* 告警电压阈值(mV): 高于此值判有烟 */
#define SMOKE_CLEAR_MV      1800    /* 解除电压阈值(mV): 低于此值判无烟 */
#define SMOKE_DEBOUNCE_N    3       /* 连续N次采样满足才翻转(防毛刺) */

/* 传感器注册表项(app_gateway.c注册) */
extern const sensor_t sensor_smoke;

#endif /* SENSOR_SMOKE_H */
