#ifndef PORT_SPI_H
#define PORT_SPI_H

#include "stm32f4xx.h"
#include "osal_types.h"

/*
 * SPI资源注册表（与drv_uart_reg同模式）：
 * 芯片引脚/复用统一登记在port_spi.def，
 * 外设驱动（LCD/Flash等）通过DRV_SPI_COM_E枚举引用总线，不接触具体引脚。
 */

/* SPI资源注册表项 */
typedef struct {
    INT8U  com;                 /* 通道号，见DRV_SPI_COM_E */
    INT8U  enable;              /* 使能 */
    INT32U spi_base;            /* SPI寄存器基地址 */
    INT32U gpio_port;           /* GPIO端口基地址 */
    INT32U pin_sck;             /* SCK引脚 */
    INT8U  sck_af;              /* SCK复用号 */
    INT32U pin_miso;            /* MISO引脚 */
    INT8U  miso_af;             /* MISO复用号 */
    INT32U pin_mosi;            /* MOSI引脚 */
    INT8U  mosi_af;             /* MOSI复用号 */
    INT32U pin_cs;              /* CS引脚，0为软件片选 */
} DRV_SPI_TBL_T;

/* 统一SPI通道枚举（由port_spi.def宏展开生成） */
#ifdef BEGIN_SPI_CFG
#undef BEGIN_SPI_CFG
#endif

#ifdef END_SPI_CFG
#undef END_SPI_CFG
#endif

#ifdef SPI_DEF
#undef SPI_DEF
#endif

#define BEGIN_SPI_CFG

#define  SPI_DEF(_COM_, _ENABLE, _SPI_BASE, _GPIO_PORT, _PIN_SCK, _PIN_SCK_AF, _PIN_MISO, _PIN_MISO_AF, _PIN_MOSI, _PIN_MOSI_AF, _PIN_CS, _DESC) \
                 _COM_,

#define END_SPI_CFG

typedef enum {
    #include "port_spi.def"
    DRV_SPI_COM_MAX
} DRV_SPI_COM_E;

/*******************************************************************
** 函数名	: DRV_SPI_GetRegTblInfo
** 函数描述	: 获取对应SPI通道的配置表信息
** 参数		: [in] com: 通道号，见DRV_SPI_COM_E
** 返回		: 成功返回配置表指针，失败返回0
********************************************************************/
const DRV_SPI_TBL_T *DRV_SPI_GetRegTblInfo(INT8U com);

#endif /* PORT_SPI_H */
