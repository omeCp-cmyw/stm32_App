#ifndef DRV_UART_REG_H
#define DRV_UART_REG_H

#include "stm32f4xx.h"
#include "osal_types.h"

/*
********************************************************************************
* define struct
********************************************************************************
*/
/* 串口资源注册表项，引脚/复用/地址/DMA通道统一登记在此 */
typedef struct {
    INT8U  com;             /* 串口号，见DRV_UART_COM_E */
    INT8U  enable;          /* 使能 */
    INT8U  tx_af;           /* TX引脚复用号 */
    INT8U  rx_af;           /* RX引脚复用号 */
    INT32U uart_base;       /* 串口寄存器基地址 */
    INT32U gpio_port;       /* GPIO端口基地址 */
    INT32U tx_pin;          /* TX引脚 */
    INT32U rx_pin;          /* RX引脚 */
    INT32U dma_tx_stream;   /* 发送DMA流 */
    INT32U dma_tx_channel;  /* 发送DMA通道 */
    INT32U dma_rx_stream;   /* 接收DMA流 */
    INT32U dma_rx_channel;  /* 接收DMA通道 */
} DRV_UART_TBL_T;

/*
********************************************************************************
* 定义统一串口通道枚举（由.def文件宏展开生成）
********************************************************************************
*/
#ifdef BEGIN_UART_CFG
#undef BEGIN_UART_CFG
#endif

#ifdef END_UART_CFG
#undef END_UART_CFG
#endif

#ifdef UART_DEF
#undef UART_DEF
#endif

#define BEGIN_UART_CFG

#define  UART_DEF(_COM_, _ENABLE, _UART_BASE, _GPIO_PORT, _PIN_TX, _PIN_TX_AF, _PIN_RX, _PIN_RX_AF, _DMA_TX_STREAM, _DMA_TX_CHANNEL, _DMA_RX_STREAM, _DMA_RX_CHANNEL) \
                 _COM_,

#define END_UART_CFG

typedef enum {
    #include "drv_uart_reg.def"
    DRV_UART_COM_MAX
} DRV_UART_COM_E;

/*******************************************************************
** 函数名	: DRV_UART_GetRegTblInfo
** 函数描述	: 获取对应串口的配置表信息。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功返回配置表指针，失败返回0
********************************************************************/
const DRV_UART_TBL_T *DRV_UART_GetRegTblInfo(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_GetCfgTblMax
** 函数描述	: 获取已注册的串口个数。
** 参数		: 无
** 返回		: 注册个数
********************************************************************/
INT8U DRV_UART_GetCfgTblMax(void);

#endif /* DRV_UART_REG_H */
