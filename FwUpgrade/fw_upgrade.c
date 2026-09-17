#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "fw_upgrade.h"
#include "../Platform/drv_flash/drv_flash.h"

/*
********************************************************************************
* 固件升级主控（Ymodem本地升级写入侧）：传输通道经 FW_UPG_WriteData 写入APP备份分区，
* 下载完成后 FW_UPG_Finish 校验并写升级标志，复位交Bootloader搬运。
********************************************************************************
*/

/* 备份分区容量（Sector8~10，与APP分区等大） */
#define FW_UPG_BACKUP_SIZE      (FLASH_APP_BACKUP_END - FLASH_APP_BACKUP_START)

/* 升级标志字布局（位于FW_UPG_FLAG_ADDR，Bootloader按此约定读取）：
 * word0: 魔数FW_UPG_FLAG_MAGIC
 * word1: 固件字节数
 * word2: 固件CRC32（逐字节数据累积，标准反射型0x04C11DB7）
 */

/* 升级控制块 */
typedef struct {
    uint8_t   state;            /* 升级状态，见FW_UPG_STATE_E */
    uint32_t  fw_size;          /* 头报文声明的固件总字节数 */
    uint32_t  recv_size;        /* 已写入备份分区的字节数 */
    uint32_t  crc32;            /* 写入数据累积CRC32 */
    uint32_t  erased_addr;      /* 备份分区已擦除到的地址（懒擦除边界） */
    uint8_t   wbuf[4];          /* 4字节对齐写缓冲 */
    uint8_t   wcnt;             /* 写缓冲已有字节数 */
} FW_UPG_T;

static FW_UPG_T s_fwupg;

/*******************************************************************
** 函数名	: fw_upg_erase_before
** 函数描述	: 懒擦除：写入前按需擦除目标地址所在扇区，
**			: 已擦区域用erased_addr边界跳过，避免重复擦除。
** 参数		: [in] addr: 即将写入的地址
** 返回		: 0成功，1失败（地址越界或擦除失败）
********************************************************************/
static uint8_t fw_upg_erase_before(uint32_t addr)
{
    if (addr >= FLASH_APP_BACKUP_END) {
        return 1;                                                      /* 越界 */
    }
    if (addr < s_fwupg.erased_addr) {
        return 0;                                                      /* 该扇区已擦除 */
    }
    if (Flash_Erase_Sector_By_Addr(addr) != 0) {
        printf("[FWUPG] erase fail @0x%08X\r\n", (unsigned int)addr);
        return 1;
    }
    s_fwupg.erased_addr = (addr & ~0x1FFFFu) + 0x20000;               /* 128KB扇区边界 */
    return 0;
}

/*******************************************************************
** 函数名	: fw_upg_crc32_update
** 函数描述	: 逐字节累积计算CRC32（多项式0x04C11DB7，反射型）。
** 参数		: [in] crc:  当前CRC值
**			: [in] data: 数据指针
**			: [in] len:  字节数
** 返回		: 更新后的CRC值
********************************************************************/
static uint32_t fw_upg_crc32_update(uint32_t crc, uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t  j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/*******************************************************************
** 函数名	: fw_upg_reset
** 函数描述	: 全部状态标志恢复默认（空闲+清零），
**			: 供传输失败/收尾异常后重开窗口等待下一次新任务。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void fw_upg_reset(void)
{
    memset(&s_fwupg, 0, sizeof(FW_UPG_T));
    s_fwupg.state = FW_UPG_STATE_IDLE;
}

/*******************************************************************
** 函数名	: FW_UPG_Init
** 函数描述	: 升级主控初始化，复位状态机。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_Init(void)
{
    fw_upg_reset();
}

/*******************************************************************
** 函数名	: FW_UPG_SetBkupFlag
** 函数描述	: 写升级标志到备份寄存器BKP0R，跨复位/掉电保持，
**			: 内部完成PWR时钟使能与备份域解锁。
** 参数		: [in] flag: 标志值，传0清除标志
** 返回		: 无
********************************************************************/
void FW_UPG_SetBkupFlag(uint32_t flag)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;                                             /* 解除备份域写保护 */
    RTC->BKP0R = flag;
}

/*******************************************************************
** 函数名	: FW_UPG_StartUpdate
** 函数描述	: 启动固件升级，校验大小并复位接收上下文。
** 参数		: [in] fwsize: 固件总字节数
** 返回		: 成功true，失败false
********************************************************************/
uint8_t FW_UPG_StartUpdate(uint32_t fwsize)
{
    if ((fwsize == 0) || (fwsize > FW_UPG_BACKUP_SIZE)) {
        return 0;                                                      /* 固件大小非法 */
    }

    s_fwupg.fw_size     = fwsize;
    s_fwupg.recv_size   = 0;
    s_fwupg.crc32       = 0xFFFFFFFF;
    s_fwupg.wcnt        = 0;
    s_fwupg.erased_addr = FLASH_APP_BACKUP_START;                      /* 尚未擦除任何扇区 */
    s_fwupg.state       = FW_UPG_STATE_RECVING;

    return 1;
}

/*******************************************************************
** 函数名	: FW_UPG_StopUpdate
** 函数描述	: 停止固件升级：全部状态标志恢复默认（空闲+清零），
**			: 供传输失败后重开窗口等待下一次新任务。
** 参数		: 无
** 返回		: 无
********************************************************************/
void FW_UPG_StopUpdate(void)
{
    fw_upg_reset();
}

/*******************************************************************
** 函数名	: FW_UPG_WriteData
** 函数描述	: 写入一段固件数据到备份分区，写入前按需懒擦除目标扇区，
**			: 内部处理4字节对齐缓冲，末包超出声明大小的填充字节截断丢弃。
** 参数		: [in] data: 数据指针
**			: [in] len:  数据字节数（含填充）
** 返回		: 成功true，失败false
********************************************************************/
uint8_t FW_UPG_WriteData(uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t word;
    uint32_t wr_offs;

    if ((data == 0) || (len == 0)) {
        return 0;
    }
    if (s_fwupg.state != FW_UPG_STATE_RECVING) {
        return 0;
    }
    if (s_fwupg.recv_size + len > s_fwupg.fw_size) {
        /* Ymodem末包按128/1024补齐填充，只写入到声明大小为止 */
        if (s_fwupg.recv_size >= s_fwupg.fw_size) {
            return 0;                                                  /* 数据已收满不应再有数据 */
        }
        len = s_fwupg.fw_size - s_fwupg.recv_size;                     /* 截断填充部分 */
    }

    wr_offs = s_fwupg.recv_size;                                       /* 本包写入地址，逐字推进 */
    for (i = 0; i < len; i++) {
        s_fwupg.wbuf[s_fwupg.wcnt++] = data[i];

        if (s_fwupg.wcnt == 4) {                                       /* 凑满一字写入 */
            word = (uint32_t)s_fwupg.wbuf[0]
                 | ((uint32_t)s_fwupg.wbuf[1] << 8)
                 | ((uint32_t)s_fwupg.wbuf[2] << 16)
                 | ((uint32_t)s_fwupg.wbuf[3] << 24);
            /* 逐字检查擦除边界：包可能跨越128KB扇区边界，
               仅按包起点检查会漏掉终点所在扇区的擦除 */
            if (fw_upg_erase_before(FLASH_APP_BACKUP_START + wr_offs) != 0
             || Flash_Write_Word(FLASH_APP_BACKUP_START + wr_offs, word) != 0) {
                s_fwupg.state = FW_UPG_STATE_IDLE;
                return 0;
            }
            wr_offs      += 4;
            s_fwupg.wcnt = 0;
        }
    }

    s_fwupg.crc32     = fw_upg_crc32_update(s_fwupg.crc32, data, len);
    s_fwupg.recv_size += len;

    return 1;
}

/*******************************************************************
** 函数名	: FW_UPG_Finish
** 函数描述	: 固件接收结束：校验大小与CRC32，写升级标志后软复位，
**			: 复位后由Bootloader读标志搬运固件到APP分区。
** 参数		: 无
** 返回		: 升级结果，见FW_UPG_RESULT_E
********************************************************************/
uint8_t FW_UPG_Finish(void)
{
    uint32_t crc32;

    if (s_fwupg.state != FW_UPG_STATE_RECVING) {
        printf("[FWUPG] finish skip, state=%u\r\n", (unsigned int)s_fwupg.state);
        fw_upg_reset();
        return FW_UPG_RESULT_OTHER;
    }

    /* 末尾不足4字节的余数补0xFF写入 */
    while (s_fwupg.wcnt != 0 && s_fwupg.wcnt < 4) {
        s_fwupg.wbuf[s_fwupg.wcnt++] = 0xFF;
    }
    if (s_fwupg.wcnt == 4) {
        uint32_t word = (uint32_t)s_fwupg.wbuf[0]
                      | ((uint32_t)s_fwupg.wbuf[1] << 8)
                      | ((uint32_t)s_fwupg.wbuf[2] << 16)
                      | ((uint32_t)s_fwupg.wbuf[3] << 24);
        if (Flash_Write_Word(FLASH_APP_BACKUP_START + (s_fwupg.recv_size & ~3u), word) != 0) {
            printf("[FWUPG] finish tail write fail\r\n");
            fw_upg_reset();
            return FW_UPG_RESULT_OTHER;
        }
        s_fwupg.wcnt = 0;
    }

    if (s_fwupg.recv_size != s_fwupg.fw_size) {
        printf("[FWUPG] size mismatch, recv=%u, expect=%u\r\n",
               (unsigned int)s_fwupg.recv_size, (unsigned int)s_fwupg.fw_size);
        fw_upg_reset();
        return FW_UPG_RESULT_CHKERR;
    }

    crc32 = s_fwupg.crc32 ^ 0xFFFFFFFF;

    /* 写后自校验：回读备份区重算CRC32与累积CRC对照，仅不一致时报警 */
    {
        uint32_t rd_crc = 0xFFFFFFFF;
        uint32_t off;
        uint8_t  b[4];

        for (off = 0; off + 4 <= s_fwupg.fw_size; off += 4) {
            uint32_t rd_word = Flash_Read_Word(FLASH_APP_BACKUP_START + off);
            b[0] = (uint8_t)rd_word;
            b[1] = (uint8_t)(rd_word >> 8);
            b[2] = (uint8_t)(rd_word >> 16);
            b[3] = (uint8_t)(rd_word >> 24);
            rd_crc = fw_upg_crc32_update(rd_crc, b, 4);
        }
        if (off < s_fwupg.fw_size) {                                   /* 非4对齐余数字节 */
            uint32_t rd_word = Flash_Read_Word(FLASH_APP_BACKUP_START + off);
            uint8_t  rem = (uint8_t)(s_fwupg.fw_size - off);
            uint8_t  k;
            for (k = 0; k < rem; k++) {
                b[k] = (uint8_t)(rd_word >> (8 * k));
            }
            rd_crc = fw_upg_crc32_update(rd_crc, b, rem);
        }
        rd_crc ^= 0xFFFFFFFF;

        if (rd_crc != crc32) {
            printf("[FWUPG] readback CRC mismatch, acc=0x%08X readback=0x%08X\r\n",
                   (unsigned int)crc32, (unsigned int)rd_crc);
            printf("[FWUPG] backup head: %08X %08X %08X %08X\r\n",
                   (unsigned int)Flash_Read_Word(FLASH_APP_BACKUP_START),
                   (unsigned int)Flash_Read_Word(FLASH_APP_BACKUP_START + 4),
                   (unsigned int)Flash_Read_Word(FLASH_APP_BACKUP_START + 8),
                   (unsigned int)Flash_Read_Word(FLASH_APP_BACKUP_START + 12));
        }
    }

    /* 升级标志写入Sector4：魔数/大小/CRC32 */
    if (Flash_Erase_Sector_By_Addr(FW_UPG_FLAG_ADDR) != 0) {
        printf("[FWUPG] flag sector erase fail\r\n");
        fw_upg_reset();
        return FW_UPG_RESULT_OTHER;
    }
    if (Flash_Write_Word(FW_UPG_FLAG_ADDR + 0, FW_UPG_FLAG_MAGIC) != 0
     || Flash_Write_Word(FW_UPG_FLAG_ADDR + 4, s_fwupg.fw_size)   != 0
     || Flash_Write_Word(FW_UPG_FLAG_ADDR + 8, crc32)             != 0) {
        printf("[FWUPG] flag write fail\r\n");
        fw_upg_reset();
        return FW_UPG_RESULT_OTHER;
    }

    printf("[FWUPG] download done, size=%u, crc32=0x%08X, reset for bootloader\r\n",
           (unsigned int)s_fwupg.fw_size, (unsigned int)crc32);

    FW_UPG_SetBkupFlag(FW_UPG_FLAG_MAGIC);                             /* 备份寄存器同步置标志 */
    HAL_Delay(100);                                                    /* 等串口发送缓冲排空 */
    s_fwupg.state = FW_UPG_STATE_DONE;                                 /* 置完成态，供LCD任务展示成功画面 */
    HAL_Delay(2000);                                                   /* 复位前留2秒显示窗口（HAL_Delay忙等不阻塞调度） */
    NVIC_SystemReset();

    return FW_UPG_RESULT_SUCCESS;
}

/*******************************************************************
** 函数名	: FW_UPG_GetState
** 函数描述	: 获取当前升级状态。
** 参数		: 无
** 返回		: 升级状态，见FW_UPG_STATE_E
********************************************************************/
uint8_t FW_UPG_GetState(void)
{
    return s_fwupg.state;
}

/*******************************************************************
** 函数名	: FW_UPG_GetRecvSize
** 函数描述	: 获取已接收的固件字节数。
** 参数		: 无
** 返回		: 已收字节数
********************************************************************/
uint32_t FW_UPG_GetRecvSize(void)
{
    return s_fwupg.recv_size;
}

/*******************************************************************
** 函数名	: FW_UPG_GetFwSize
** 函数描述	: 获取头报文声明的固件总字节数。
** 参数		: 无
** 返回		: 固件总字节数，未启动升级时为0
********************************************************************/
uint32_t FW_UPG_GetFwSize(void)
{
    return s_fwupg.fw_size;
}
