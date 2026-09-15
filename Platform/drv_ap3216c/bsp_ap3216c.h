/********************************************************************************
**
** 文件名:     bsp_ap3216c.h
** 版权所有:   无
** 文件描述:   该模块主要实现AP3216C光照三合一传感器驱动接口定义
**
*********************************************************************************/


#ifndef __BSP_AP3216C_H
#define __BSP_AP3216C_H

#include <stdint.h>

/* AP3216C传感器接口 */
int      ap3216c_init(void);                 /* 初始化(I2C1硬件+芯片配置) */
float    ap3216c_read_ambient_light(void);   /* 读环境光强度(lux) */
uint16_t ap3216c_read_ps_data(void);         /* 读接近数据(10bit+状态位) */
uint16_t ap3216c_read_ir_data(void);         /* 读红外数据 */

#endif /* __BSP_AP3216C_H */
