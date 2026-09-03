#include "drv_uart_reg.h"

/*
********************************************************************************
* 定义串口配置表（.def文件宏展开生成表项）
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
   {(INT8U)_COM_, (INT8U)_ENABLE, (INT8U)_PIN_TX_AF, (INT8U)_PIN_RX_AF, (INT32U)_UART_BASE, (INT32U)_GPIO_PORT, (INT32U)_PIN_TX, (INT32U)_PIN_RX, \
    (INT32U)_DMA_TX_STREAM, (INT32U)_DMA_TX_CHANNEL, (INT32U)_DMA_RX_STREAM, (INT32U)_DMA_RX_CHANNEL},

#define END_UART_CFG


/* 指针常量初始化const表，屏蔽armcc #1296-D提示 */
#pragma diag_suppress 1296
static const DRV_UART_TBL_T s_uart_tbl[] = {
    #include "drv_uart_reg.def"
    {0}
};
#pragma diag_default 1296

/*******************************************************************
** 函数名	: DRV_UART_GetRegTblInfo
** 函数描述	: 获取对应串口的配置表信息。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功返回配置表指针，失败返回0
********************************************************************/
const DRV_UART_TBL_T *DRV_UART_GetRegTblInfo(INT8U com)
{
    if (com >= DRV_UART_COM_MAX) {
        return 0;
    }
    return &s_uart_tbl[com];
}

/*******************************************************************
** 函数名	: DRV_UART_GetCfgTblMax
** 函数描述	: 获取已注册的串口个数。
** 参数		: 无
** 返回		: 注册个数
********************************************************************/
INT8U DRV_UART_GetCfgTblMax(void)
{
    return DRV_UART_COM_MAX;
}
