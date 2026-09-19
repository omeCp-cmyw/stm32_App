#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ymodem.h"
#include "fw_upgrade.h"
#include "../../Platform/drv_uart/bsp_upgrade_usart.h"
#include "../../OSAL/osal.h"

/*
********************************************************************************
* 串口Ymodem升级通道接收侧：
* 逐字节组包，报文完整后校验CRC16与序号；
* 周期扫描由Ymodem任务1ms驱动；应答经发送侧接口发出，
* 固件数据流经fw_upgrade主控写入备份分区。
* 协议时序为标准双EOT版：
* 'C'握手 → 0号头包(文件名+大小)ACK+'C' → 数据包逐帧ACK/NAK →
* EOT首回NAK/重发回ACK后主动发'C' → 空文件名结束包ACK+CAN CAN收尾。
********************************************************************************
*/

/*
********************************************************************************
* 宏定义
********************************************************************************
*/
#define YM_SCAN_PERIOD_MS     1            /* 接收扫描周期1ms */
#define YM_MAX_ERRORS         5            /* 连续坏包上限，超限双CAN取消 */

/* 时序参数（单位ms） */
#define YM_RX_TIMEOUT_MS        5000     /* 接收态超时放弃 */
#define YM_END_PKT_TIMEOUT_MS   3000     /* 等结束包超时 */

/* 升级窗口：上电即发'C'握手，窗口内无传输则关闭退回正常态 */
#define YM_ARM_TIMEOUT_MS       10000    /* 升级窗口时长ms */

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
    uint8_t   state;           /* 组包状态，见YM_STATE_E */
    uint8_t   pkt_type;        /* 当前报文头类型 */
    uint16_t  data_len;        /* 当前报文数据区长度 */
    uint16_t  data_idx;        /* 数据区已收字节数 */
    uint16_t  skip_cnt;        /* 丢弃状态剩余字节数 */
    uint8_t   seq;             /* 包序号 */
    uint8_t   seq_inv;         /* 包序号反码 */
    uint16_t  crc;             /* 包尾CRC16 */
    uint8_t   expect_seq;      /* 期望的下一个包序号 */
    uint8_t   file_done;       /* 数据文件已收满（以头包声明大小为准），允许处理EOT */
    uint8_t   eot_first;       /* EOT双次确认标志：首个EOT回NAK，重发EOT才ACK（标准Ymodem） */
    uint8_t   session_begin;   /* 会话已建立（头包已接受），此前坏包不计错误 */
    uint32_t  errors;          /* 会话建立后连续坏包计数，收包成功清零 */
    uint8_t   armed;           /* 升级等待态标志，0为正常态 */
    uint8_t   last_can;        /* 上一字节是CAN标志（连续两个CAN才取消） */
    uint32_t  arm_tick;        /* 进入升级等待态的ms节拍 */
    uint32_t  last_rx_tick;    /* 最近收到字节的ms节拍 */
    uint32_t  eot2_tick;       /* 第二个EOT应答后的ms节拍，用于结束包超时判断 */
} YM_RCB_T;

/*
********************************************************************************
* 接收模块变量
********************************************************************************
*/
static YM_RCB_T s_rcb;
static uint8_t s_pkt_data[1024];                                       /* 组包数据区 */

static void ymodem_recv_byte(uint8_t byte);
static void ymodem_packet_done(void);
extern void ymodem_send_poll(void);

/*******************************************************************
** 函数名	: ymodem_crc16
** 函数描述	: 计算CRC16（CCITT，多项式0x1021，初值0）。
** 参数		: [in] data: 数据指针
**			: [in] len:  字节数
** 返回		: CRC16值
********************************************************************/
static uint16_t ymodem_crc16(uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;
    uint32_t i;
    uint8_t  j;

    for (i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

/*******************************************************************
** 函数名	: ymodem_on_soh
** 函数描述	: 收到128字节数据包头，开始组包。
** 参数		: [in] byte: 报文头字节，未使用
** 返回		: 无
********************************************************************/
static void ymodem_on_soh(uint8_t byte)
{
    (void)byte;

    s_rcb.pkt_type = YMODEM_SOH;
    s_rcb.data_len = 128;
    s_rcb.data_idx = 0;
    s_rcb.state    = YM_STATE_SEQ;
}

/*******************************************************************
** 函数名	: ymodem_on_stx
** 函数描述	: 收到1024字节数据包头，开始组包。
** 参数		: [in] byte: 报文头字节，未使用
** 返回		: 无
********************************************************************/
static void ymodem_on_stx(uint8_t byte)
{
    (void)byte;

    s_rcb.pkt_type = YMODEM_STX;
    s_rcb.data_len = 1024;
    s_rcb.data_idx = 0;
    s_rcb.state    = YM_STATE_SEQ;
}

/*******************************************************************
** 函数名	: ymodem_on_eot
** 函数描述	: EOT双次确认：数据未收满的EOT忽略；
**			: 首个EOT回NAK，重发EOT后回ACK并主动发'C'。
** 参数		: [in] byte: 报文头字节，未使用
** 返回		: 无
********************************************************************/
static void ymodem_on_eot(uint8_t byte)
{
    (void)byte;

    if (s_rcb.file_done == 0) {
        printf("[YMODEM] EOT ignored, data not complete\r\n");
        return;                                                        /* 数据未收满的EOT忽略 */
    }

    if (s_rcb.eot_first != 0) {
        s_rcb.eot_first = 0;
        FW_UPG_YM_SendByte(YMODEM_NAK);                                /* 第一次确认：NAK请求重发 */
        return;
    }

    FW_UPG_YM_SendByte(YMODEM_ACK);                                    /* 第二次确认：文件传输成功 */
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                                /* 主动发'C'询问是否还有文件 */
    s_rcb.eot2_tick = osal_get_time_ms();
}

/*******************************************************************
** 函数名	: ymodem_on_can
** 函数描述	: 收到发送方取消指令，中止升级。调用前已确认连续两个CAN。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_on_can(void)
{
    printf("[YMODEM] canceled by sender\r\n");
    FW_UPG_StopUpdate();
    FW_UPG_YM_Init();
}

/*******************************************************************
** 函数名	: ymodem_abort_session
** 函数描述	: 中止本次升级会话（发送方敲'A'/'a'用户终止），双CAN告知后复位通道。
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
** 函数名	: ymodem_head_packet
** 函数描述	: 处理0号头报文：解析固件大小并启动升级。
**			: 本函数内不做耗时操作，擦除推迟到写数据时执行。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_head_packet(void)
{
    uint32_t fwsize;
    uint16_t namelen;

    /* 文件名为空表示Ymodem结束包，数据已收完则直接收尾 */
    if (s_pkt_data[0] == 0) {
        printf("[YMODEM] end packet received, finish\r\n");
        FW_UPG_YM_ListAck(YMODEM_CRC_REQ);                             /* 应答确认摘除握手重发句柄 */
        FW_UPG_YM_SendByte(YMODEM_ACK);
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN告知发送方结束 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_Finish();
        FW_UPG_YM_Init();
        return;
    }

    namelen = (uint16_t)strlen((char *)s_pkt_data);
    if (namelen + 1 >= s_rcb.data_len) {
        printf("[YMODEM] head name too long, NAK\r\n");
        FW_UPG_YM_SendByte(YMODEM_NAK);
        return;
    }

    fwsize = (uint32_t)strtoul((char *)s_pkt_data + namelen + 1, 0, 10);

    /* 头包信息先打印 */
    printf("[YMODEM] head packet: name=%s, size=%u bytes\r\n",
           (char *)s_pkt_data, (unsigned int)fwsize);

    FW_UPG_YM_ListAck(YMODEM_CRC_REQ);                                 /* 发送方已响应，摘除握手重发句柄 */
    FW_UPG_YM_SendByte(YMODEM_ACK);                                    /* 先确认头包，擦除耗时不阻塞应答 */
    if (FW_UPG_StartUpdate(fwsize) == 0) {
        printf("[YMODEM] start update failed, size=%u\r\n", (unsigned int)fwsize);
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN取消 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_StopUpdate();                                           /* 状态恢复默认 */
        FW_UPG_YM_Init();
        return;
    }

    s_rcb.expect_seq    = 1;
    s_rcb.session_begin = 1;
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                                /* 立即请求数据包 */
}

/*******************************************************************
** 函数名	: ymodem_data_packet
** 函数描述	: 处理数据报文，写入备份分区。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_data_packet(void)
{
    if (FW_UPG_WriteData(s_pkt_data, s_rcb.data_len) == 0) {
        printf("[YMODEM] write flash failed\r\n");
        FW_UPG_YM_SendByte(YMODEM_CAN);                                /* 连续两个CAN取消 */
        FW_UPG_YM_SendByte(YMODEM_CAN);
        FW_UPG_StopUpdate();
        FW_UPG_YM_Init();
        return;
    }

    s_rcb.expect_seq++;
    FW_UPG_YM_SendByte(YMODEM_ACK);

    /* 每32包打印一次进度 */
    if (((uint16_t)(s_rcb.expect_seq - 1) & 0x1F) == 0) {
        printf("[YMODEM] progress %u/%u bytes\r\n",
               (unsigned int)FW_UPG_GetRecvSize(), (unsigned int)FW_UPG_GetFwSize());
    }
}

/*******************************************************************
** 函数名	: ymodem_packet_done
** 函数描述	: 报文收齐后校验CRC16与序号，按包序号处理。
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

    if (s_rcb.seq == (uint8_t)(s_rcb.expect_seq - 1)) {
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
**			: 后续握手挂发送重发句柄每秒重发，等待发送方发起传输。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_arm(void)
{
    /* OTA升级进行中不开窗 */
    if (FW_UPG_GetState() != FW_UPG_STATE_IDLE) {
        printf("[YMODEM] ota upgrade busy, arm rejected\r\n");
        return;
    }

    s_rcb.armed        = 1;
    s_rcb.arm_tick     = osal_get_time_ms();
    s_rcb.last_rx_tick = s_rcb.arm_tick;

    printf("[YMODEM] upgrade window open, send firmware within %us\r\n",
           (unsigned int)(YM_ARM_TIMEOUT_MS / 1000));
    FW_UPG_YM_SendByte(YMODEM_CRC_REQ);                                /* 直发首个握手 */
    FW_UPG_YM_ListSend(YMODEM_CRC_REQ, YM_ARM_TIMEOUT_MS / 1000, 1000); /* 后续握手每秒重发 */
}

/*******************************************************************
** 函数名	: FW_UPG_YM_IsArmed
** 函数描述	: 查询是否处于升级等待态（含接收中）。
** 参数		: 无
** 返回		: 1升级等待/接收中，0正常态
********************************************************************/
uint8_t FW_UPG_YM_IsArmed(void)
{
    return s_rcb.armed;
}

/*******************************************************************
** 函数名	: ymodem_scan_poll
** 函数描述	: 接收扫描（Ymodem任务1ms调用）：
**			: 正常态仅排空残留串口数据；升级等待态数据交组包状态机，
**			: 再做窗口超时/接收超时检查。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void ymodem_scan_poll(void)
{
    uint32_t now;
    uint32_t drain;
    int32_t  ch;

    if (s_rcb.armed == 0) {
        /* 正常态：仅排空残留串口数据；排空量进函数时快照，
           防止持续字节流下空转出不去 */
        drain = UPGRADE_USART_GetRecvBytes();
        while (drain > 0) {
            UPGRADE_USART_ReadChar();
            drain--;
        }
        return;
    }

    /* 升级等待态/接收态：读出数据交组包状态机 */
    drain = UPGRADE_USART_GetRecvBytes();
    while (drain > 0) {
        ch = UPGRADE_USART_ReadChar();
        if (ch >= 0) {
            ymodem_recv_byte((uint8_t)ch);
        }
        drain--;
    }

    /* 超时基准在泵数据之后取，防止无符号减法回绕误判超时 */
    now = osal_get_time_ms();

    /* 升级窗口内无传输，关闭窗口 */
    if (FW_UPG_GetState() == FW_UPG_STATE_IDLE) {
        if (now - s_rcb.arm_tick >= YM_ARM_TIMEOUT_MS) {
            printf("[YMODEM] upgrade window closed, reset to re-enter\r\n");
            s_rcb.armed = 0;
        }
        return;                                                        /* 等待期由窗口超时管 */
    }

    /* 第二个EOT后没收到结束包，超时直接收尾 */
    if (s_rcb.file_done != 0 && s_rcb.eot2_tick != 0
        && now - s_rcb.eot2_tick >= YM_END_PKT_TIMEOUT_MS
        && s_rcb.state == YM_STATE_HEAD && now - s_rcb.last_rx_tick >= YM_END_PKT_TIMEOUT_MS) {
        printf("[YMODEM] end packet missing, finish anyway\r\n");
        FW_UPG_Finish();
        FW_UPG_YM_Init();
        return;
    }

    /* 接收态数据流中断超时 */
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
** 函数描述	: Ymodem通道初始化：复位组包状态机与发送重发句柄，
**			: 随后打开升级窗口。Ymodem任务由应用层创建。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_YM_Init(void)
{
    memset(&s_rcb, 0, sizeof(YM_RCB_T));
    s_rcb.state        = YM_STATE_HEAD;
    s_rcb.eot_first    = 1;
    s_rcb.last_rx_tick = osal_get_time_ms();

    ymodem_arm();                                                      /* 复位即开升级窗口，窗口超时关闭后不再进入 */
}

/*******************************************************************
** 函数名	: FW_UPG_YM_Task
** 函数描述	: Ymodem任务入口：1ms周期驱动接收扫描与发送重发时序。
** 参数		: [in] arg: 任务参数，未使用
** 返回		: 无
********************************************************************/
void FW_UPG_YM_Task(void *arg)
{
    (void)arg;

    while (1) {
        ymodem_scan_poll();
        ymodem_send_poll();
        osal_task_delay(YM_SCAN_PERIOD_MS);
    }
}

/*******************************************************************
** 函数名	: ymodem_recv_byte
** 函数描述	: 接收一个字节并驱动组包状态机，报文完整后校验处理。
** 参数		: [in] byte: 接收字节
** 返回		: 无
********************************************************************/
static void ymodem_recv_byte(uint8_t byte)
{
    s_rcb.last_rx_tick = osal_get_time_ms();
    /* 取消检测，只在报文头位置判定 */
    if (s_rcb.state == YM_STATE_HEAD) {
        if (byte == YMODEM_CAN) {
            if (s_rcb.last_can != 0) {                                 /* 连续两个CAN才取消 */
                s_rcb.last_can = 0;
                ymodem_on_can();
                return;
            }
            s_rcb.last_can = 1;
            return;
        }
        if ((byte == 0x41) || (byte == 0x61)) {                        /* 'A'/'a'用户终止 */
            ymodem_abort_session();
            return;
        }
    }
    s_rcb.last_can = 0;

    switch (s_rcb.state) {
    case YM_STATE_HEAD:
        /* 按报文头类型启动组包，非协议字节忽略 */
        if (byte == YMODEM_SOH) {
            ymodem_on_soh(byte);
        } else if (byte == YMODEM_STX) {
            ymodem_on_stx(byte);
        } else if (byte == YMODEM_EOT) {
            ymodem_on_eot(byte);
        }
        break;

    case YM_STATE_SEQ:
        s_rcb.seq   = byte;
        s_rcb.state = YM_STATE_SEQINV;
        break;

    case YM_STATE_SEQINV:
        s_rcb.seq_inv = byte;
        if ((uint8_t)(s_rcb.seq ^ s_rcb.seq_inv) != 0xFF) {
            printf("[YMODEM] seq inv err, seq=%02X, inv=%02X\r\n",
                   (unsigned int)s_rcb.seq, (unsigned int)s_rcb.seq_inv);
            FW_UPG_YM_SendByte(YMODEM_NAK);                            /* 序号反码错请求重发 */
            s_rcb.skip_cnt = (uint16_t)(s_rcb.data_len + 2);           /* 丢弃整包剩余字节 */
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
        s_rcb.crc   = (uint16_t)((uint16_t)byte << 8);
        s_rcb.state = YM_STATE_CRCLO;
        break;

    case YM_STATE_CRCLO:
        s_rcb.crc   = (uint16_t)(s_rcb.crc | byte);
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
