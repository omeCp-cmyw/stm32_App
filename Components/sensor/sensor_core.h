#ifndef SENSOR_CORE_H
#define SENSOR_CORE_H

#include <stdint.h>

/*
 * 传感器框架：注册表 + 定时采集引擎。
 * 传感器驱动按sensor_t注册，sensor_poll()由主循环周期性调用，
 * 到期自动执行read()并回调物模型，新增传感器只需注册一行。
 * 依赖：osal.h提供的时间基准，物模型写入接口由App层注册。
 */

/* 统一数据格式: 数值+单位+时间戳(物模型层使用) */
typedef struct {
    const char *key;            /* 属性名(见product_def.h) */
    int32_t     ivalue;         /* 整数值(浮点由上层转换) */
    uint32_t    fvalue_x10;     /* 浮点值x10(1位小数精度) */
    uint8_t     is_float;       /* 1浮点, 0整数 */
    uint32_t    ts_ms;          /* 采集时间戳 */
} sensor_sample_t;

/* 传感器接口 */
typedef struct {
    const char *name;           /* 名称, 如"temp/hum" */
    int     (*init)(void);      /* 初始化, 返回0成功 */
    int     (*read)(sensor_sample_t *out); /* 采集, 0成功 */
    uint32_t period_ms;         /* 采集周期(ms) */
} sensor_t;

/* 物模型写入回调(由App层app_devmodel注册) */
typedef void (*sensor_apply_fn)(const sensor_sample_t *s);

/*******************************************************************
** 函数名	: sensor_core_init
** 函数描述	: 传感器框架初始化(注册物模型写入回调)，须在osal_init后
** 参数		: [in] apply: 采集数据写入回调
** 返回		: 无
********************************************************************/
void sensor_core_init(sensor_apply_fn apply);

/*******************************************************************
** 函数名	: sensor_register
** 函数描述	: 注册传感器，内部调用其init
** 参数		: [in] s: 传感器接口(静态生命周期)
** 返回		: 1成功, 0失败(表满或init失败)
********************************************************************/
int sensor_register(const sensor_t *s);

/*******************************************************************
** 函数名	: sensor_poll
** 函数描述	: 采集引擎轮询(主循环调用)：到期传感器执行read并回调物模型
** 参数		: 无
** 返回		: 无
********************************************************************/
void sensor_poll(void);

#endif /* SENSOR_CORE_H */
