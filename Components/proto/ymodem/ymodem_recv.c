#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ymodem.h"
#include "fw_upgrade.h"
#include "drv_uart.h"
#include "drv_systick.h"
#include "osal.h"

/*
********************************************************************************
* 串口Ymodem升级通道接收侧：
* RCB接收控制块+逐字节组包，报文完整后校验CRC16与序号，
* 查注册表（PROTOCOL_REG_T）派发处理；周期扫描由软件定时器驱动，
* 应答经发送侧接口发出，
* 解出的固件数据流经fw_upgrade主控写入备份分区。
* 协议时序为标准严格双EOT版：
* 'C'握手 → 0号头包(文件名+大小)ACK+'C' → 数据包逐帧ACK/NAK →
* EOT首回NAK/重发回ACK后主动发'C' → 空文件名结束包ACK+CAN CAN收尾。
********************************************************************************
*/

/*
********************************************************************************
* 宏定义
********************************************************************************
*/
#define YM_MAX_REG            80           /* 注册表上限 */
#define YM_SCAN_PERIOD_MS     1            /* 接收扫描周期1ms */
#define YM_MAX_ERRORS         5            /* 会话建立后连续坏包上限，超限双CAN取消 */

/* 时序参数（单位ms） */
#define YM_RX_TIMEOUT_MS        5000     /* 接收态超时放弃 */
#define YM_END_PKT_TIMEOUT_MS   3000     /* 第二个EOT应答后等待结束包超时，超时直接收尾 */

/* 升级窗口：复位初始化后立即进入升级等待态发'C'握手（对应ST参考上电即等待），
   窗口内无有效传输则关闭并退回静默正常态，需再次复位才能重新进入 */
#define YM_ARM_TIMEOUT_MS       10000    /* 升级窗口时长：'C'重发次数=本值/1000 */

/*
********************************************************************************
* 接收模块数据结构
********************************************************************************
*/
/* 组包状态 */
typedef enum {
    YM_STATE_HEAD = 0,         /* 等待报文头 */
    YM_STATE_SEQ,              /* 接收包序号 */
    YM_STATE_SEQINV,           /* 接收包序号反码 */
    YM_STATE_DATA,             /* 接收数据区 */
    YM_STATE_CRCHI,            /* 接收CRC高字节 */
    YM_STATE_CRCLO,            /* 接收CRC低字节 */
    YM_STATE_SKIP,             /* 校验失败丢弃剩余字节 */
    YM_STATE_MAX
} YM_STATE_E;

/* 接收控制块 */
typedef struct {
    INT8U   state;             /* 组包状态，见YM_STATE_E */
    INT8U   pkt_type;          /* 当前报文头类型 */
    INT16U  data_len;          /* 当前报文数据区长度 */
    INT16U  data_idx;          /* 数据区已收字节数 */
    INT16U  skip_cnt;          /* 丢弃状态剩余字节数 */
    INT8U   seq;               /* 包序号 */
    INT8U   seq_inv;           /* 包序号反码 */
    INT16U  crc;               /* 包尾CRC16 */
    INT8U   expect_seq;        /* 期望的下一个包序号 */
    INT8U   file_done;         /* 数据文件已收满（以头包声明大小为准），允许处理EOT */
    INT8U   eot_first;         /* EOT双次确认标志：首个EOT回NAK，重发EOT才ACK（标准Ymodem） */
    INT8U   session_begin;     /* 会话已建立（头包已接受），此前坏包不计错误 */
    INT32U  errors;            /* 会话建立后连续坏包计数，收包成功清零 */
    INT8U   armed;             /* 升级等待态标志，0为正常态 */
    INT8U   last_can;          /* 上一字节是CAN标志（对齐ST参考：连续两个CAN才取消） */
    INT32U  arm_tick;          /* 进入升级等待态的ms节拍 */
    INT32U  last_rx_tick;      /* 最近收到字节的ms节拍 */
    INT32U  eot2_tick;         /* 第二个EOT应答后的ms节拍，用于结束包超时判断 */
} YM_RCB_T;

/*
********************************************************************************
* 接收模块变量
********************************************************************************
*/
static YM_RCB_T s_rcb;
static INT8U s_pkt_data[1024];                                       /* 组包数据区 */

/* 报文类型注册表（freelist/usedlist单链管理） */
static PROTOCOL_REG_T s_reg_tbl[YM_MAX_REG];
static PROTOCOL_REG_T *s_usedlist, *s_freelist;
static INT8U s_scantmr = 0xFF;                                       /* 接收扫描定时器 */

static void ymodem_recv_byte(INT8U byte);
static void ymodem_packet_done(void);

/*******************************************************************
** 函数名	: ymodem_crc16
** 函数描述	: 计算CRC16（CCITT，多项式0x1021，初值0）。
** 参数		: [in] data: 数据指针
**			: [in] len:  字节数
** 返回		: CRC16值
********************************************************************/
static INT16U ymodem_crc16(INT8U *data, INT32U len)
{
    INT16U crc = 0;
    INT32U i;
    INT8U  j;

    for (i = 0; i < len; i++) {
        crc = (INT16U)(crc ^ ((INT16U)data[i] << 8));
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (INT16U)((crc << 1) ^ 0x1021);
            } else {
                crc = (INT16U)(crc << 1);
            }
        }
    }

    return crc;
}

/*******************************************************************
** 函数名	: ymodem_on_soh
** 函数描述	: 收到128字节数据包头，开始组包。
** 参数		: [in] type: 报文头类型
**			: [in] ptr/len: 载荷，Ymodem为定长包头部派发时无载荷恒为0
** 返回		: 无
********************************************************************/
static void ymodem_on_soh(INT16U type, INT8U *ptr, INT16U len)
{
    type = type;
    ptr  = ptr;
    len  = len;

    s_rcb.pkt_type = YMODEM_SOH;
    s_rcb.data_len = 128;
    s_rcb.data_idx = 0;
    s_rcb.state    = YM_STATE_SEQ;
}

/*******************************************************************
** 函数名	: ymodem_on_stx
** 函数描述	: 收到1024字节数据包头，开始组包。
** 参数		: [in] type: 报文头类型
**			: [in] ptr/len: 载荷，Ymodem为定长包头部派发时无载荷恒为0
** 返回		: 无
********************************************************************/
static void ymodem_on_stx(INT16U type, INT8U *ptr, INT16U len)
{
    type = type;
    ptr  = ptr;
    len  = len;

    s_rcb.pkt_type = YMODEM_STX;
    s_rcb.data_len = 1024;
    s_rcb.data_idx = 0;
    s_rcb.state    = YM_STATE_SEQ;
}

/*******************************************************************
** 函数名	: ymodem_on_eot
** 函数描述	: EOT双次确认（标准Ymodem严格时序）：数据未收满的EOT忽略；
**			: 首个EOT回NAK，发送方重发EOT后回ACK并主动发'C'请求结束帧。
** 参数		: [in] type: 报文头类型
**			: [in] ptr/len: 载荷，EOT无载荷恒为0
** 返回		: 无
********************************************************************/
static void ymodem_on_eot(INT16U type, INT8U *ptr, INT16U len)
{
    type = type;
    ptr  = ptr;
    len  = len;

    if (s_rcb.file_done == 0) {
        printf("[YMODEM] EOT ignored, data not complete\r\n");
        return;                                                        /* 数据未收满的EOT忽略 */
    }

    if (s_rcb.eot_first != 0) {
        s_rcb.eot_first = 0;
        printf("[YMODEM] EOT 1st, NAK for resend\r\n");
        FW_UPG_YM_SendByte(YMODEM_NAK);                                /* 第一次确认：NAK请求重发 */
        return;
    }

    printf("[YMODEM] EOT 2nd, ACK+'C' wait end pkt\r\n");
    FW_UPG_YM_SendByte(YMODEM_ACK);                                    /* 第二次确认：文件传输成功 */
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                                /* 主动发'C'询问是否还有文件 */
    s_rcb.eot2_tick = SYSTICK_GetMsTick();
}

/*******************************************************************
** 函数名	: ymodem_on_can
** 函数描述	: 收到发送方取消指令，中止升级（对齐ST参考Receive_Packet的case CA：
**			: 调用前已确认连续两个CAN）。
** 参数		: [in] type: 报文头类型
**			: [in] ptr/len: 载荷，CAN无载荷恒为0
** 返回		: 无
********************************************************************/
static void ymodem_on_can(INT16U type, INT8U *ptr, INT16U len)
{
    type = type;
    ptr  = ptr;
    len  = len;

    printf("[YMODEM] canceled by sender\r\n");
    FW_UPG_StopUpdate();
    FW_UPG_YM_Init();
}

/*******************************************************************
** 函数名	: ymodem_abort_session
** 函数描述	: 中止本次升级会话（对齐ST参考Receive_Packet的case ABORT：
**			: 发送方敲'A'/'a'用户终止），双CAN告知后复位通道。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_abort_session(void)
{
    printf("[YMODEM] aborted by user\r\n");
    FW_UPG_YM_SendByte(YMODEM_CAN);                                    /* 连续两个CAN取消 */
    FW_UPG_YM_SendByte(YMODEM_CAN);
    FW_UPG_StopUpdate();
    FW_UPG_YM_Init();
}

/*******************************************************************
** 函数名	: ymodem_session_fail
** 函数描述	: 会话建立后连续坏包超限，双CAN告知发送方后复位通道。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_session_fail(void)
{
    printf("[YMODEM] too many errors, cancel\r\n");
    FW_UPG_YM_SendByte(YMODEM_CAN);                                    /* 连续两个CAN取消 */
    FW_UPG_YM_SendByte(YMODEM_CAN);
    FW_UPG_StopUpdate();
    FW_UPG_YM_Init();
}

/*******************************************************************
** 函数名	: FW_UPG_YM_Register
** 函数描述	: 接收处理注册。
** 参数		: [in] type:    报文头类型
**			: [in] handler: 报文处理函数
** 返回		: 注册成功true，失败false
********************************************************************/
BOOLEAN FW_UPG_YM_Register(INT16U type, void (*handler)(INT16U type, INT8U *ptr, INT16U len))
{
    PROTOCOL_REG_T *curptr;

    curptr = s_usedlist;
    while (curptr != 0) {                                                      /* 检查重复注册 */
        OSAL_ASSERT((curptr->type != type), RETURN_FALSE);
        curptr = curptr->next;
    }

    OSAL_ASSERT(handler != 0, RETURN_FALSE);                                   /* 处理函数不能为空 */
    OSAL_ASSERT(s_freelist != 0, RETURN_FALSE);                                /* 注册表已满 */

    curptr = s_freelist;                                                       /* 摘取空闲节点 */
    if (curptr != 0) {
        s_freelist        = curptr->next;
        curptr->next      = s_usedlist;
        s_usedlist        = curptr;
        curptr->type      = type;
        curptr->c_handler = handler;                                           /* 处理函数 */
        curptr->b_handler = handler;                                           /* 处理函数备份 */
        return true;
    } else {
        return false;
    }
}

/*******************************************************************
** 函数名	: ymodem_head_packet
** 函数描述	: 处理0号头报文：解析固件大小并启动升级。
**			: 本函数内不做任何长耗时操作，扇区擦除推迟到WriteData懒执行，
**			: 避免阻塞头包应答。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_head_packet(void)
{
    INT32U fwsize;
    INT16U namelen;

    /* 文件名为空表示Ymodem结束包，数据已收完则直接收尾 */
    if (s_pkt_data[0] == 0) {
        printf("[YMODEM] end packet received, finish\r\n");
        FW_UPG_YM_ListAck(YMODEM_CRC_REQ, _SUCCESS);                  /* 应答确认摘除握手节点 */
        FW_UPG_YM_SendByte(YMODEM_ACK);
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN告知发送方结束 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_Finish();
        FW_UPG_YM_Init();
        return;
    }

    namelen = (INT16U)strlen((char *)s_pkt_data);
    if (namelen + 1 >= s_rcb.data_len) {
        printf("[YMODEM] head name too long, NAK\r\n");
        FW_UPG_YM_SendByte(YMODEM_NAK);
        return;
    }

    fwsize = (INT32U)strtoul((char *)s_pkt_data + namelen + 1, 0, 10);

    /* 头包解析结果：文件名+固件大小，应答前先打印，
       即使后续StartUpdate失败也已留痕便于核对发送方文件 */
    printf("[YMODEM] head packet: name=%s, size=%u bytes\r\n",
           (char *)s_pkt_data, (unsigned int)fwsize);

    FW_UPG_YM_ListAck(YMODEM_CRC_REQ, _SUCCESS);                      /* 发送方已响应，摘除握手节点 */
    FW_UPG_YM_SendByte(YMODEM_ACK);                                   /* 先确认头包，擦除耗时不阻塞应答 */
    if (FW_UPG_StartUpdate(fwsize) == FALSE) {
        printf("[YMODEM] start update failed, size=%u\r\n", (unsigned int)fwsize);
        FW_UPG_YM_SendByte(YMODEM_CAN);                               /* 连续两个CAN取消 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_StopUpdate();                                          /* 状态标志恢复默认（多文件场景下可能残留RECVING） */
        FW_UPG_YM_Init();
        return;
    }

    printf("[YMODEM] start download, file=%s size=%u\r\n", (char *)s_pkt_data, (unsigned int)fwsize);
    s_rcb.expect_seq    = 1;
    s_rcb.session_begin = 1;
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                               /* 立即请求数据包，扇区擦除在写入时懒执行 */
}

/*******************************************************************
** 函数名	: ymodem_data_packet
** 函数描述	: 处理数据报文，写入备份分区。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_data_packet(void)
{
    if (FW_UPG_WriteData(s_pkt_data, s_rcb.data_len) == FALSE) {
        printf("[YMODEM] write flash failed\r\n");
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN取消 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_StopUpdate();
        FW_UPG_YM_Init();
        return;
    }

    s_rcb.expect_seq++;
    FW_UPG_YM_SendByte(YMODEM_ACK);

    /* 进度打印：每32个数据包一条（1K包约每32KB），频率极低不影响吞吐，
       用于定位传输停滞位置（如扇区懒擦除卡顿、发送方停发） */
    if (((INT16U)(s_rcb.expect_seq - 1) & 0x1F) == 0) {
        printf("[YMODEM] progress %u/%u bytes\r\n",
               (unsigned int)FW_UPG_GetRecvSize(), (unsigned int)FW_UPG_GetFwSize());
    }
}

/*******************************************************************
** 函数名	: ymodem_packet_done
** 函数描述	: 报文收齐后校验CRC16与序号，按包序号派发处理。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_packet_done(void)
{
    if (ymodem_crc16(s_pkt_data, s_rcb.data_len) != s_rcb.crc) {
        printf("[YMODEM] crc err, seq=%u, type=%02X, errors=%u\r\n",
               (unsigned int)s_rcb.seq, (unsigned int)s_rcb.pkt_type, (unsigned int)s_rcb.errors);
        FW_UPG_YM_SendByte(YMODEM_NAK);                                /* CRC错请求重发 */
        if (s_rcb.session_begin != 0) {
            s_rcb.errors++;
        }
        return;
    }

    /* 数据收满后发送方发0号空文件名包作批量结束包，不受数据序号约束 */
    if (s_rcb.file_done != 0 && s_rcb.seq == 0) {
        ymodem_head_packet();
        return;
    }

    if (s_rcb.seq == (INT8U)(s_rcb.expect_seq - 1)) {
        printf("[YMODEM] dup pkt seq=%u, re-ack\r\n", (unsigned int)s_rcb.seq);
        FW_UPG_YM_SendByte(YMODEM_ACK);                                /* 重复包仅应答，避免发送方卡死 */
        return;
    }
    if (s_rcb.seq != s_rcb.expect_seq) {
        printf("[YMODEM] seq err, expect=%u, got=%u\r\n",
               (unsigned int)s_rcb.expect_seq, (unsigned int)s_rcb.seq);
        FW_UPG_YM_SendByte(YMODEM_NAK);                                /* 序号错请求重发 */
        if (s_rcb.session_begin != 0) {
            s_rcb.errors++;
        }
        return;
    }

    s_rcb.errors = 0;                                                  /* 收包成功清零 */

    if (s_rcb.seq == 0) {
        ymodem_head_packet();                                          /* 0号头报文 */
    } else {
        ymodem_data_packet();                                          /* 固件数据报文 */
    }

    /* 数据文件大小以头报文声明为准，收满后置位允许处理EOT */
    if (FW_UPG_GetRecvSize() >= FW_UPG_GetFwSize() && FW_UPG_GetState() == FW_UPG_STATE_RECVING) {
        s_rcb.file_done = 1;
    }
}

/*******************************************************************
** 函数名	: ymodem_arm
** 函数描述	: 打开升级窗口：进入升级等待态直发首个'C'，
**			: 后续握手挂发送队列周期重发，等待发送方发起传输。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_arm(void)
{
    s_rcb.armed    = 1;
    s_rcb.arm_tick = SYSTICK_GetMsTick();
    s_rcb.last_rx_tick = s_rcb.arm_tick;

    printf("[YMODEM] upgrade window open, send firmware within %us\r\n",
           (unsigned int)(YM_ARM_TIMEOUT_MS / 1000));
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                                /* 直发首个握手 */
    FW_UPG_YM_ListSend(YMODEM_CRC_REQ, YM_ARM_TIMEOUT_MS / 1000, 1000, 0); /* 后续握手挂队列每秒重发 */
}

/*******************************************************************
** 函数名	: FW_UPG_YM_IsArmed
** 函数描述	: 查询是否处于升级等待态（含接收中）。
** 参数		: 无
** 返回		: true升级等待/接收中，false正常态
********************************************************************/
BOOLEAN FW_UPG_YM_IsArmed(void)
{
    return s_rcb.armed;
}

/*******************************************************************
** 函数名	: ymodem_scan_tmr
** 函数描述	: 接收扫描定时器回调：
**			: 正常态仅排空残留串口数据；升级等待态数据交组包状态机，
**			: 再做窗口超时/接收超时检查。
** 参数		: [in] index: 定时器创建时的参数指针，未使用
** 返回		: 无
********************************************************************/
static void ymodem_scan_tmr(void *index)
{
    INT32U now;
    INT32U drain;
    index = index;
    if (s_rcb.armed == 0) {
        /* 正常态：升级窗口已关闭不再进入，仅排空残留串口数据防环缓冲积压；
           必须直接return——此处每1ms执行一次，落入下方IDLE分支会因
           arm_tick为旧值反复触发关窗打印造成刷屏。
           排空量必须进函数时快照定死：中断持续往环缓冲填数据，若循环条件每圈
           重新查GetRecvBytes，串口线有持续字节流时used永不为0，
           会在本回调内空转出不去，阻塞主循环的定时器调度 */
        drain = DRV_UART_GetRecvBytes(YM_UART_COM);
        while (drain > 0) {
            DRV_UART_ReadChar(YM_UART_COM);
            drain--;
        }
        return;
    }

    /* 升级等待态/接收态：读出数据交组包状态机，
       同样快照定死本轮处理量，避免发送方持续灌包时回调内滞留过久 */
    drain = DRV_UART_GetRecvBytes(YM_UART_COM);
    while (drain > 0) {
        ymodem_recv_byte((INT8U)DRV_UART_ReadChar(YM_UART_COM));
        drain--;
    }

    /* 超时基准必须在泵数据之后取：recv_byte逐字节把last_rx_tick刷到当前ms，
       若泵前取样则泵跨毫秒节拍时now < last_rx_tick，无符号减法回绕成巨值，
       误触发下方超时判定，正在收数据也会被判中止 */
    now = SYSTICK_GetMsTick();

    /* 升级窗口内无有效传输，关闭窗口退回静默正常态 */
    if (FW_UPG_GetState() == FW_UPG_STATE_IDLE) {
        if (now - s_rcb.arm_tick >= YM_ARM_TIMEOUT_MS) {
            printf("[YMODEM] upgrade window closed, reset to re-enter\r\n");
            s_rcb.armed = 0;
        }
        return;                                                        /* 等待期由窗口超时兜底，不受下方接收超时约束 */
    }

    /* 第二个EOT已应答后发送方未发结束包，超时后直接收尾（兼容不发结束包的发送方） */
    if (s_rcb.file_done != 0 && s_rcb.eot2_tick != 0
        && now - s_rcb.eot2_tick >= YM_END_PKT_TIMEOUT_MS
        && s_rcb.state == YM_STATE_HEAD && now - s_rcb.last_rx_tick >= YM_END_PKT_TIMEOUT_MS) {
        printf("[YMODEM] end packet missing, finish anyway\r\n");
        FW_UPG_Finish();
        FW_UPG_YM_Init();
        return;
    }

    /* 接收态数据流中断超时（仅升级进行中生效：等待发送方发起传输的
       握手期由30秒窗口超时兜底，否则用户晚点几秒发送就会被误判中止） */
    if (now - s_rcb.last_rx_tick >= YM_RX_TIMEOUT_MS) {
        printf("[YMODEM] receive timeout, abort\r\n");
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN取消 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_StopUpdate();
        FW_UPG_YM_Init();
        return;
    }

    /* 会话建立后连续坏包超限 */
    if (s_rcb.session_begin != 0 && s_rcb.errors > YM_MAX_ERRORS) {
        ymodem_session_fail();
    }
}

/*******************************************************************
** 函数名	: FW_UPG_YM_Init
** 函数描述	: Ymodem通道初始化：
**			: 复位组包状态机，建立注册表并注册协议报文类型，
**			: 创建并启动收发扫描定时器，随后打开升级窗口。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_YM_Init(void)
{
    INT8U i;

    memset(&s_rcb, 0, sizeof(YM_RCB_T));
    s_rcb.state        = YM_STATE_HEAD;
    s_rcb.eot_first    = 1;
    s_rcb.last_rx_tick = SYSTICK_GetMsTick();

    /* 建立注册表（s_reg_tbl链化） */
    for (i = 0; i < YM_MAX_REG - 1; i++) {
        s_reg_tbl[i].next      = &s_reg_tbl[i + 1];
        s_reg_tbl[i].type      = 0;
        s_reg_tbl[i].c_handler = 0;
        s_reg_tbl[i].b_handler = 0;
    }
    s_reg_tbl[i].next      = 0;
    s_reg_tbl[i].type      = 0;
    s_reg_tbl[i].c_handler = 0;
    s_reg_tbl[i].b_handler = 0;

    s_freelist = &s_reg_tbl[0];
    s_usedlist = 0;

    /* 注册Ymodem协议报文类型（CAN/ABORT不走注册表，
       在ymodem_recv_byte报文头位置直接拦截判定，对齐ST参考） */
    FW_UPG_YM_Register(YMODEM_SOH, ymodem_on_soh);
    FW_UPG_YM_Register(YMODEM_STX, ymodem_on_stx);
    FW_UPG_YM_Register(YMODEM_EOT, ymodem_on_eot);

    /* 接收扫描定时器只创建一次，重启复位倒计时 */
    if (s_scantmr == 0xFF) {
        printf("[YMODEM] scan tmr create\r\n");
        s_scantmr = osal_timer_create((void *)0, ymodem_scan_tmr);
    }
    osal_timer_start(s_scantmr, YM_SCAN_PERIOD_MS, 1);

    FW_UPG_YM_SendInit();

    ymodem_arm();                                                      /* 复位即开升级窗口，窗口超时关闭后不再进入 */
}

/*******************************************************************
** 函数名	: ymodem_recv_byte
** 函数描述	: 接收一个字节并驱动组包状态机，
**			: 报文头查注册表派发，报文完整后校验处理。
** 参数		: [in] byte: 接收字节
** 返回		: 无
********************************************************************/
static void ymodem_recv_byte(INT8U byte)
{
    PROTOCOL_REG_T *curptr;

    s_rcb.last_rx_tick = SYSTICK_GetMsTick();
    /* 取消/终止检测（对齐ST参考Receive_Packet，仅在报文头位置判定，
       避免固件数据流中合法的连续0x18误杀） */
    if (s_rcb.state == YM_STATE_HEAD) {
        if (byte == YMODEM_CAN) {
            if (s_rcb.last_can != 0) {                                 /* 连续两个CAN才取消 */
                s_rcb.last_can = 0;
                ymodem_on_can(YMODEM_CAN, 0, 0);
                return;
            }
            s_rcb.last_can = 1;
            return;
        }
        if ((byte == 0x41) || (byte == 0x61)) {                        /* 'A'/'a'用户终止（ABORT1/ABORT2） */
            ymodem_abort_session();
            return;
        }
    }
    s_rcb.last_can = 0;

    switch (s_rcb.state) {
    case YM_STATE_HEAD:
        /* 遍历注册表查报文头启动组包，非协议字节忽略 */
        curptr = s_usedlist;
        while (curptr != 0) {
            if (curptr->type == byte) {
                OSAL_ASSERT((curptr->c_handler != 0), RETURN_VOID);                  /* 注册的处理函数不能为空 */
                OSAL_ASSERT((curptr->c_handler == curptr->b_handler), RETURN_VOID);  /* 注册的处理函数有效 */
                curptr->c_handler(byte, 0, 0);                                     /* Ymodem定长包，载荷组包完成后从控制块读取 */
                break;
            }
            curptr = curptr->next;
        }
        break;

    case YM_STATE_SEQ:
        s_rcb.seq   = byte;
        s_rcb.state = YM_STATE_SEQINV;
        break;

    case YM_STATE_SEQINV:
        s_rcb.seq_inv = byte;
        if ((INT8U)(s_rcb.seq ^ s_rcb.seq_inv) != 0xFF) {
            printf("[YMODEM] seq inv err, seq=%02X, inv=%02X\r\n",
                   (unsigned int)s_rcb.seq, (unsigned int)s_rcb.seq_inv);
            FW_UPG_YM_SendByte(YMODEM_NAK);                            /* 序号反码错请求重发 */
            s_rcb.skip_cnt = (INT16U)(s_rcb.data_len + 2);             /* 丢弃整包剩余字节 */
            s_rcb.state    = YM_STATE_SKIP;
        } else {
            s_rcb.state = YM_STATE_DATA;
        }
        break;

    case YM_STATE_DATA:
        s_pkt_data[s_rcb.data_idx++] = byte;
        if (s_rcb.data_idx >= s_rcb.data_len) {
            s_rcb.state = YM_STATE_CRCHI;
        }
        break;

    case YM_STATE_CRCHI:
        s_rcb.crc   = (INT16U)((INT16U)byte << 8);
        s_rcb.state = YM_STATE_CRCLO;
        break;

    case YM_STATE_CRCLO:
        s_rcb.crc   = (INT16U)(s_rcb.crc | byte);
        s_rcb.state = YM_STATE_HEAD;
        ymodem_packet_done();
        break;

    case YM_STATE_SKIP:
        if (s_rcb.skip_cnt > 0) {
            s_rcb.skip_cnt--;
        }
        if (s_rcb.skip_cnt == 0) {
            s_rcb.state = YM_STATE_HEAD;
            if (s_rcb.session_begin != 0) {                            /* 坏包计一次错误 */
                s_rcb.errors++;
            }
        }
        break;

    default:
        s_rcb.state = YM_STATE_HEAD;
        break;
    }
}
