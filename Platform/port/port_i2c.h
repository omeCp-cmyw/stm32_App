#ifndef PORT_I2C_H
#define PORT_I2C_H

#include "stm32f4xx.h"
#include "osal_types.h"

/*
 * I2C资源注册表（与drv_uart_reg同模式）：
 * 芯片引脚/复用/速率统一登记在port_i2c.def，
 * 传感器驱动通过DRV_I2C_COM_E枚举引用总线，不接触具体引脚。
 */

/* I2C资源注册表项 */
typedef struct {
    INT8U  com;                 /* 通道号，见DRV_I2C_COM_E */
    INT8U  enable;              /* 使能 */
    INT32U i2c_base;            /* I2C寄存器基地址 */
    INT32U speed_khz;           /* 总线速率(KHz) */
    INT32U gpio_port;           /* GPIO端口基地址 */
    INT32U pin_scl;             /* SCL引脚 */
    INT8U  scl_af;              /* SCL复用号 */
    INT32U pin_sda;             /* SDA引脚 */
    INT8U  sda_af;              /* SDA复用号 */
} DRV_I2C_TBL_T;

/* 统一I2C通道枚举（由port_i2c.def宏展开生成） */
#ifdef BEGIN_I2C_CFG
#undef BEGIN_I2C_CFG
#endif

#ifdef END_I2C_CFG
#undef END_I2C_CFG
#endif

#ifdef I2C_DEF
#undef I2C_DEF
#endif

#define BEGIN_I2C_CFG

#define  I2C_DEF(_COM_, _ENABLE, _I2C_BASE, _SPEED_KHZ, _GPIO_PORT, _PIN_SCL, _PIN_SCL_AF, _PIN_SDA, _PIN_SDA_AF, _DESC) \
                 _COM_,

#define END_I2C_CFG

typedef enum {
    #include "port_i2c.def"
    DRV_I2C_COM_MAX
} DRV_I2C_COM_E;

/*******************************************************************
** 函数名	: DRV_I2C_GetRegTblInfo
** 函数描述	: 获取对应I2C通道的配置表信息
** 参数		: [in] com: 通道号，见DRV_I2C_COM_E
** 返回		: 成功返回配置表指针，失败返回0
********************************************************************/
const DRV_I2C_TBL_T *DRV_I2C_GetRegTblInfo(INT8U com);

#endif /* PORT_I2C_H */
