#ifndef FW_YMODEM_H
#define FW_YMODEM_H

#include "os_type.h"

/*
********************************************************************************
* 串口Ymodem升级通道公共定义（层级原样对齐野火yx_mmi_recv/yx_mmi_send，
* 接收组包与发送应答分文件实现：fw_ymodem_recv.c / fw_ymodem_send.c）：
* 设备为被升级方（接收方）：正常态不主动发串口，串口收到升级触发命令后
* 进入升级等待态发'C'握手，超时自动退回；
* 接收侧逐字节组包后查注册表派发，扫描由软件定时器驱动（对应野火ScanTmrProc）；
* 发送侧CELL_T节点+三链表内存池管理（对应野火ListSend/DirSend/ListAck），
* 时序敏感应答（ACK/NAK/CAN）直发，'C'握手带重发挂发送队列；
* 解出的固件数据流经fw_upgrade主控写入备份分区。
********************************************************************************
*/

/* Ymodem协议字节定义 */
#define YMODEM_SOH              0x01     /* 128字节数据包头 */
#define YMODEM_STX              0x02     /* 1024字节数据包头 */
#define YMODEM_EOT              0x04     /* 传输结束 */
#define YMODEM_ACK              0x06
#define YMODEM_NAK              0x15
#define YMODEM_CAN              0x18
#define YMODEM_CRC_REQ          'C'      /* 接收方握手字符，请求CRC模式 */

/* Ymodem升级物理通道：USART3/RS232-U3（中断收发，与日志串口USART1分离，
   编号见drv_uart_reg.h的DRV_UART_COM_E），改通道只需改此处 */
#define YM_UART_COM             DRV_UART_COM_2

/* 报文头类型注册结构体（原样借鉴野火PROTOCOL_REG_T，freelist/usedlist注册管理） */
typedef struct protocolreg {
    struct protocolreg *next;
    INT16U type;                                       /* 报文头类型 */
    void (*c_handler)(INT16U type, INT8U *ptr, INT16U len);   /* 处理入口 */
    void (*b_handler)(INT16U type, INT8U *ptr, INT16U len);   /* 处理入口备份，注册时与c_handler一致 */
} PROTOCOL_REG_T;

/*******************************************************************
** 函数名	: FW_UPG_YM_Init
** 函数描述	: Ymodem通道初始化（对应野火InitRecv/InitSend）：
**			: 建立注册表并注册协议报文类型，复位组包状态机与发送控制块，
**			: 创建并启动收发扫描定时器。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_YM_Init(void);

/*******************************************************************
** 函数名	: FW_UPG_YM_Register
** 函数描述	: 接收处理注册（对应野火YX_MMI_Register）。
** 参数		: [in] type:    报文头类型
**			: [in] handler: 报文处理函数
** 返回		: 注册成功true，失败false
********************************************************************/
BOOLEAN FW_UPG_YM_Register(INT16U type, void (*handler)(INT16U type, INT8U *ptr, INT16U len));

/*******************************************************************
** 函数名	: FW_UPG_YM_IsArmed
** 函数描述	: 查询是否处于升级等待态（含接收中）。
** 参数		: 无
** 返回		: true升级等待/接收中，false正常态静默
********************************************************************/
BOOLEAN FW_UPG_YM_IsArmed(void);

/*******************************************************************
** 函数名	: FW_UPG_YM_SendByte
** 函数描述	: 即时直发一个控制字符（对应野火DirSend），
**			: ACK/NAK/CAN等时序敏感应答不进发送队列。
** 参数		: [in] ch: 控制字符
** 返回		: 无
********************************************************************/
void FW_UPG_YM_SendByte(INT8U ch);

/*******************************************************************
** 函数名	: FW_UPG_YM_ListSend
** 函数描述	: 发送侧挂链表发送（对应野火ListSend）：控制字符入发送队列，
**			: 由扫描定时器按窗口/重发时序发出，入队后立即触发首次发送。
** 参数		: [in] type:    控制字符（Ymodem协议字节）
**			: [in] ct_send: 重发次数，0为只发一次
**			: [in] ct_time: 重发等待时长，单位ms，0为入队后首周期即发
**			: [in] fp:      发送结果通知回调，结果见os_type.h的_SUCCESS/_OVERTIME
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN FW_UPG_YM_ListSend(INT16U type, INT8U ct_send, INT16U ct_time, void (*fp)(INT8U));

/*******************************************************************
** 函数名	: FW_UPG_YM_ListAck
** 函数描述	: 发送队列应答确认（对应野火ListAck）：
**			: 按报文类型查找等待/就绪队列摘除节点并回调结果。
** 参数		: [in] type:   报文类型（控制字符）
**			: [in] result: 结果，见os_type.h的_SUCCESS/_OVERTIME
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN FW_UPG_YM_ListAck(INT16U type, INT8U result);

/*******************************************************************
** 函数名	: FW_UPG_YM_SendInit
** 函数描述	: 发送侧初始化（对应野火InitSend），创建并启动发送扫描定时器，
**			: 由FW_UPG_YM_Init内部调用。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_YM_SendInit(void);

#endif /* FW_YMODEM_H */
