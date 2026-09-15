#include "ymodem.h"
#include "../../Platform/drv_uart/bsp_upgrade_usart.h"
#include "../../OSAL/osal.h"

/*
********************************************************************************
* 串口Ymodem升级通道发送侧（FreeRTOS任务适配版）：
* 设备为被升级方，出向数据均为单字节控制字符：
* ACK/NAK/CAN时序敏感应答阻塞直发，'C'握手带重发走单节点重发句柄，
* 由Ymodem任务每1ms调用ymodem_send_poll推进重发时序。
* 备份工程原实现为三链表发送队列（freelist/readylist/waitlist），
* 本工程出向仅单字符控制字节，重发句柄单节点即可等效覆盖。
********************************************************************************
*/

/* 发送重发句柄 */
static struct {
    uint8_t  ch;          /* 待重发控制字符 */
    uint8_t  ct_send;     /* 剩余重发次数 */
    uint16_t flowtime;    /* 重发间隔时长，单位ms */
    uint32_t last_tick;   /* 上次发送时刻（ms） */
} s_resend;

/*******************************************************************
** 函数名	: ymodem_send_poll
** 函数描述	: 发送扫描（Ymodem任务1ms调用）：
**			: 重发句柄存在且到达重发间隔时重发控制字符。
** 参数		: 无
** 返回		: 无
********************************************************************/
void ymodem_send_poll(void)
{
    uint32_t now;

    if (s_resend.ct_send == 0) {
        return;                                                        /* 无待重发字符 */
    }

    now = osal_get_time_ms();
    if (now - s_resend.last_tick >= s_resend.flowtime) {
        UPGRADE_USART_WriteByte(s_resend.ch);
        s_resend.last_tick = now;
        s_resend.ct_send--;
    }
}

/*******************************************************************
** 函数名	: FW_UPG_YM_SendByte
** 函数描述	: 即时直发一个控制字符，
**			: ACK/NAK/CAN等时序敏感应答不进发送队列。
** 参数		: [in] ch: 控制字符
** 返回		: 无
********************************************************************/
void FW_UPG_YM_SendByte(uint8_t ch)
{
    UPGRADE_USART_WriteByte(ch);
}

/*******************************************************************
** 函数名	: FW_UPG_YM_ListSend
** 函数描述	: 挂发送重发句柄：控制字符立即首发一次，随后按周期重发，
**			: 发送方响应头报文后经FW_UPG_YM_ListAck摘除。
** 参数		: [in] type:    控制字符（Ymodem协议字节）
**			: [in] ct_send: 重发次数，0为只发一次
**			: [in] ct_time: 重发等待时长，单位ms，0为入队后立即重发
** 返回		: 成功1，失败0
********************************************************************/
uint8_t FW_UPG_YM_ListSend(uint8_t type, uint8_t ct_send, uint16_t ct_time)
{
    if (s_resend.ct_send != 0) {
        return 0;                                                      /* 重发句柄已被占用 */
    }

    s_resend.ch        = type;
    s_resend.ct_send   = ct_send;
    s_resend.flowtime  = ct_time;
    s_resend.last_tick = osal_get_time_ms();

    UPGRADE_USART_WriteByte(type);                                     /* 立即首发一次 */

    return 1;
}

/*******************************************************************
** 函数名	: FW_UPG_YM_ListAck
** 函数描述	: 发送重发句柄应答确认：按报文类型查找并摘除。
** 参数		: [in] type: 报文类型（控制字符）
** 返回		: 摘除成功1，未找到0
********************************************************************/
uint8_t FW_UPG_YM_ListAck(uint8_t type)
{
    if (s_resend.ct_send != 0 && s_resend.ch == type) {
        s_resend.ct_send = 0;                                          /* 摘除重发句柄 */
        return 1;
    }
    return 0;
}
