#include "ymodem.h"
#include "drv_uart.h"
#include "tool_list.h"
#include "osal.h"

/*
********************************************************************************
* 串口Ymodem升级通道发送侧：
* CELL_T发送节点+freelist/readylist/waitlist三链表内存池统一管理，
* SendTmrProc周期扫描（等待队列重发超时/就绪队列窗口等待/发送就绪节点），
* 发送队列挂链表入队、即时直发、应答消节点。
* Ymodem适配：设备为被升级方（接收方），出向数据均为单字节控制字符，
* 节点数据区内联于CELL_T，不使用动态内存；
* ACK/NAK/CAN时序敏感走DirSend即时直发，'C'握手带重发挂发送队列，
* 发送方响应头报文后经ListAck摘除握手节点。
* 链表复用tools/tool_list。
********************************************************************************
*/

/*
********************************************************************************
* 宏定义
********************************************************************************
*/
#define YM_NUM_MEM            8          /* 发送节点池容量 */
#define YM_MAX_SIZE           2          /* 节点数据长度上限，CAN×2 */

#define YM_SEND_PERIOD_MS     1          /* 发送扫描周期1ms */

/*
********************************************************************************
* 发送模块数据结构
********************************************************************************
*/
/* 发送控制节点（数据区内联不用动态内存） */
typedef struct {
    INT16U type;                       /* 报文类型，Ymodem即控制字符 */
    INT16U seq;                        /* 发送流水号 */
    INT8U  ct_send;                    /* 重发次数 */
    INT16U ct_time;                    /* 重发等待计时 */
    INT16U flowtime;                   /* 重发等待时长，单位ms */
    INT16U slen;                       /* 数据长度 */
    INT8U  data[YM_MAX_SIZE];          /* 数据区 */
    void   (*fp)(INT8U result);        /* 发送结果通知回调 */
} YM_CELL_T;

/*
********************************************************************************
* 发送模块变量
********************************************************************************
*/
static struct {
    NODE_T reserve;
    YM_CELL_T cell;
} s_memory[YM_NUM_MEM];

static INT8U s_sendtmr = 0xFF;
static INT8U s_diagreg;                                                        /* 诊断已注册标志 */
static LIST_T s_waitlist, s_readylist, s_freelist;
static INT16U s_seq;

/*******************************************************************
** 函数名	: ymodem_del_cell
** 函数描述	: 删除发送节点：数据区内联无需释放，
**			: 节点归还空闲链表后回调发送结果。
** 参数		: [in] cell:   发送节点
**			: [in] result: 结果，见osal_types.h的_SUCCESS/_OVERTIME
** 返回		: 无
********************************************************************/
static void ymodem_del_cell(YM_CELL_T *cell, INT8U result)
{
    void (*fp)(INT8U result);

    fp = cell->fp;

    LS_LIST_AppendListEle(&s_freelist, (INT8U *)cell);
    if (fp != 0) {
        fp(result);
    }
}

/*******************************************************************
** 函数名	: ymodem_send_tmr
** 函数描述	: 发送扫描定时器回调：
**			: 扫描等待队列处理重发超时，扫描就绪队列处理窗口等待，
**			: 发送就绪节点；队列清空后停止定时器。
** 参数		: [in] index: 定时器创建时的参数指针，未使用
** 返回		: 无
********************************************************************/
static void ymodem_send_tmr(void *index)
{
    YM_CELL_T *cell, *next;

    index = index;

    if (LS_LIST_GetNodeNum(&s_waitlist) + LS_LIST_GetNodeNum(&s_readylist) == 0) {
        osal_timer_stop(s_sendtmr);
        return;
    }

    osal_timer_start(s_sendtmr, YM_SEND_PERIOD_MS, 1);

    cell = (YM_CELL_T *)LS_LIST_GetListHead(&s_waitlist);                         /* 扫描等待队列：重发超时 */
    for (;;) {
        if (cell == 0) break;
        if (++cell->ct_time > cell->flowtime) {                                   /* 重发等待超时 */
            cell->ct_time = 0;
            next = (YM_CELL_T *)LS_LIST_DeleListEle(&s_waitlist, (INT8U *)cell);
            if (--cell->ct_send == 0) {                                           /* 重发次数耗尽 */
                ymodem_del_cell(cell, _OVERTIME);
            } else {
                LS_LIST_AppendListEle(&s_readylist, (INT8U *)cell);
            }
            cell = next;
        } else {
            cell = (YM_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);
        }
    }

    cell = (YM_CELL_T *)LS_LIST_GetListHead(&s_readylist);                        /* 扫描就绪队列：窗口等待超时 */
    for (;;) {
        if (cell == 0) break;
        if (++cell->ct_time > cell->flowtime) {                                   /* 等待时间超时 */
            cell->ct_time = 0;
            if (cell->ct_send == 0) {
                next = (YM_CELL_T *)LS_LIST_DeleListEle(&s_readylist, (INT8U *)cell); /* 从就绪队列删除节点 */
                ymodem_del_cell(cell, _OVERTIME);
                cell = next;
                continue;
            } else {
                cell->ct_send--;
            }
        }
        cell = (YM_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);
    }

    if (LS_LIST_GetNodeNum(&s_readylist) > 0) {                                   /* 就绪队列存在待发送节点 */
        cell = (YM_CELL_T *)LS_LIST_GetListHead(&s_readylist);
        for (;;) {
            if (cell == 0) break;

            if (cell->slen <= DRV_UART_LeftOfSendbuf(YM_UART_COM)) {               /* 发送缓冲足够则发出 */
                DRV_UART_WriteBlock(YM_UART_COM, cell->data, cell->slen);
            }

            next = (YM_CELL_T *)LS_LIST_DeleListEle(&s_readylist, (INT8U *)cell);
            if (cell->ct_send > 0) {                                              /* 需要重发等待应答的节点 */
                cell->ct_time = 0;
                LS_LIST_AppendListEle(&s_waitlist, (INT8U *)cell);
            } else {                                                              /* 无需重发的节点 */
                ymodem_del_cell(cell, _SUCCESS);
            }
            cell = next;
        }
    }
}

/*******************************************************************
** 函数名	: ymodem_diagnose
** 函数描述	: 发送侧诊断：
**			: 三链表节点总数守恒，有在途节点时定时器必须在跑。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_diagnose(void)
{
    INT32U count;

    count = LS_LIST_GetNodeNum(&s_waitlist) + LS_LIST_GetNodeNum(&s_readylist) + LS_LIST_GetNodeNum(&s_freelist);
    OSAL_ASSERT(count == sizeof(s_memory)/sizeof(s_memory[0]), RETURN_VOID);

    if (LS_LIST_GetNodeNum(&s_waitlist) + LS_LIST_GetNodeNum(&s_readylist) > 0) {
        OSAL_ASSERT(osal_timer_is_run(s_sendtmr), RETURN_VOID);
    }
}

/*******************************************************************
** 函数名	: FW_UPG_YM_SendInit
** 函数描述	: 发送侧初始化：
**			: 重建三链表内存池并复位流水号，定时器只创建一次，注册诊断。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_YM_SendInit(void)
{
    s_seq = 0;
    LS_LIST_Init(&s_waitlist);
    LS_LIST_Init(&s_readylist);
    LS_LIST_CreateList(&s_freelist, (INT8U *)s_memory, sizeof(s_memory)/sizeof(s_memory[0]), sizeof(s_memory[0]));

    if (s_sendtmr == 0xFF) {                                                      /* 定时器只创建一次 */
        s_sendtmr = osal_timer_create((void *)0, ymodem_send_tmr);
    }

    if (s_diagreg == 0) {                                                         /* 诊断只注册一次 */
        s_diagreg = 1;
        osal_register_diag(ymodem_diagnose);
    }
}

/*******************************************************************
** 函数名	: FW_UPG_YM_ListSend
** 函数描述	: 发送侧挂链表发送：控制字符入发送队列，
**			: 由扫描定时器按窗口/重发时序发出，入队后立即触发首次发送。
** 参数		: [in] type:    控制字符（Ymodem协议字节）
**			: [in] ct_send: 重发次数，0为只发一次
**			: [in] ct_time: 重发等待时长，单位ms，0为入队后首周期即发
**			: [in] fp:      发送结果通知回调，结果见osal_types.h的_SUCCESS/_OVERTIME
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN FW_UPG_YM_ListSend(INT16U type, INT8U ct_send, INT16U ct_time, void (*fp)(INT8U))
{
    YM_CELL_T *cell;

    cell = (YM_CELL_T *)LS_LIST_DeleListHead(&s_freelist);                        /* 申请空闲节点 */
    if (cell == 0) {
        return FALSE;
    }

    cell->type     = type;
    cell->seq      = s_seq++;
    cell->ct_send  = ct_send;
    cell->flowtime = ct_time;                                                     /* 重发等待时长 */
    cell->ct_time  = 0;
    cell->fp       = fp;
    cell->slen     = 1;
    cell->data[0]  = (INT8U)type;                                                 /* 控制字符即发送内容 */

    LS_LIST_AppendListEle(&s_readylist, (INT8U *)cell);                           /* 发送节点挂就绪队列 */

    if (!osal_timer_is_run(s_sendtmr)) {
        osal_timer_start(s_sendtmr, YM_SEND_PERIOD_MS, 1);
    }

    return TRUE;
}

/*******************************************************************
** 函数名	: FW_UPG_YM_ListAck
** 函数描述	: 发送队列应答确认：
**			: 按报文类型查找等待/就绪队列摘除节点并回调结果。
** 参数		: [in] type:   报文类型（控制字符）
**			: [in] result: 结果，见osal_types.h的_SUCCESS/_OVERTIME
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN FW_UPG_YM_ListAck(INT16U type, INT8U result)
{
    YM_CELL_T *cell;

    cell = (YM_CELL_T *)LS_LIST_GetListHead(&s_waitlist);
    for (;;) {                                                                    /* 查找等待队列 */
        if (cell == 0) break;
        if ((cell->type == type) && (cell->ct_send > 0)) {                        /* 查找到匹配的节点 */
            LS_LIST_DeleListEle(&s_waitlist, (INT8U *)cell);
            ymodem_del_cell(cell, result);
            return TRUE;
        } else {
            cell = (YM_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);                /* 扫描下个节点 */
        }
    }

    cell = (YM_CELL_T *)LS_LIST_GetListHead(&s_readylist);                        /* 查找就绪队列 */
    for (;;) {
        if (cell == 0) break;
        if ((cell->type == type) && (cell->ct_send > 0)) {                        /* 查找到匹配节点 */
            LS_LIST_DeleListEle(&s_readylist, (INT8U *)cell);
            ymodem_del_cell(cell, result);
            return TRUE;
        } else {
            cell = (YM_CELL_T *)LS_LIST_GetNextEle((INT8U *)cell);                /* 扫描下个节点 */
        }
    }
    return FALSE;
}

/*******************************************************************
** 函数名	: FW_UPG_YM_SendByte
** 函数描述	: 即时直发一个控制字符，
**			: ACK/NAK/CAN等时序敏感应答不进发送队列。
** 参数		: [in] ch: 控制字符
** 返回		: 无
********************************************************************/
void FW_UPG_YM_SendByte(INT8U ch)
{
    DRV_UART_WriteChar(YM_UART_COM, ch);
}
