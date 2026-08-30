#ifndef DRV_UART_H
#define DRV_UART_H

#include "os_type.h"
#include "drv_uart_reg.h"

/* 流控模式 */
typedef enum {
    DRV_UART_FCM_NULL = 0,     /* 无流控 */
    DRV_UART_FCM_XONXOFF,      /* 软件流控 */
    DRV_UART_FCM_RTS,          /* 硬件流控 */
    DRV_UART_FCM_MAX
} DRV_UART_FCM_E;

/* 数据位 */
typedef enum {
    DRV_UART_DATABIT_NULL = 0,
    DRV_UART_DATABIT_7,
    DRV_UART_DATABIT_8,
    DRV_UART_DATABIT_MAX
} DRV_UART_DATABIT_E;

/* 停止位 */
typedef enum {
    DRV_UART_STOPBIT_NULL = 0,
    DRV_UART_STOPBIT_1,
    DRV_UART_STOPBIT_2,
    DRV_UART_STOPBIT_MAX
} DRV_UART_STOPBIT_E;

/* 校验位 */
typedef enum {
    DRV_UART_PARITY_NONE_YX = 0,   /* 无校验 */
    DRV_UART_PARITY_EVEN_YX,       /* 偶校验 */
    DRV_UART_PARITY_ODD_YX,        /* 奇校验 */
    DRV_UART_PARITY_YX_MAX
} DRV_UART_PARITY_E;

/* 统一串口编号见drv_uart_reg.h的DRV_UART_COM_E（.def注册表生成） */

/* 串口配置参数 */
typedef struct {
    INT32U com;        /* 串口号，见DRV_UART_COM_E */
    INT32U baud;       /* 波特率，1200~115200 */
    INT32U parity;     /* 校验位，见DRV_UART_PARITY_E */
    INT32U databit;    /* 数据位，见DRV_UART_DATABIT_E */
    INT32U stopbit;    /* 停止位，见DRV_UART_STOPBIT_E */

    INT32U rx_len;     /* 接收环形缓冲区长度 */
    INT32U tx_len;     /* 发送环形缓冲区长度 */
} DRV_UART_CFG_T;

/*******************************************************************
** 函数名	: DRV_UART_InitDrv
** 函数描述	: 初始化串口驱动，按默认参数打开所有串口。
** 参数		: 无
** 返回		: 无
********************************************************************/
void DRV_UART_InitDrv(void);

/*******************************************************************
** 函数名	: DRV_UART_OpenUart
** 函数描述	: 打开串口并初始化缓冲区。
** 参数		: [in] cfg: 串口配置参数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_OpenUart(DRV_UART_CFG_T *cfg);

/*******************************************************************
** 函数名	: DRV_UART_CloseUart
** 函数描述	: 关闭串口。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_CloseUart(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_ReadChar
** 函数描述	: 从接收环形缓冲读取一个字节。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功返回数据，失败返回-1。
********************************************************************/
INT32S DRV_UART_ReadChar(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_GetRecvBytes
** 函数描述	: 获取接收环形缓冲中已收字节数。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 已收字节数。
********************************************************************/
INT32U DRV_UART_GetRecvBytes(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_RxByteProcess
** 函数描述	: 接收数据处理入口，当前为调试打印，后续改为Ymodem协议处理。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 无
********************************************************************/
void DRV_UART_RxByteProcess(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_WriteChar
** 函数描述	: 发送一个字节，写入发送环形缓冲区后由中断发出。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] data: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteChar(INT8U com, INT8U data);

/*******************************************************************
** 函数名	: DRV_UART_WriteCharWait
** 函数描述	: 阻塞方式发送一个字节，等待发送完成。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] data: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteCharWait(INT8U com, INT8U data);

/*******************************************************************
** 函数名	: DRV_UART_WriteBlock
** 函数描述	: 发送一段数据，写入发送环形缓冲区后由中断发出。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] sptr: 数据指针
**			: [in] slen: 数据长度
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteBlock(INT8U com, INT8U *sptr, INT16U slen);

/*******************************************************************
** 函数名	: DRV_UART_LeftOfSendbuf
** 函数描述	: 获取发送缓冲区剩余空间。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 剩余空间字节数
********************************************************************/
INT32U DRV_UART_LeftOfSendbuf(INT8U com);

/*******************************************************************
** 函数名	: DRV_UART_MutePrint
** 函数描述	: 开关printf输出：升级期间静音避免日志混入升级串口。
** 参数		: [in] mute: 1静音，0恢复
** 返回		: 无
********************************************************************/
void DRV_UART_MutePrint(INT8U mute);

#endif /* DRV_UART_H */
