#include <stdio.h>
#include "drv_flash.h"

/* 演示用：数据存放地址与测试数据 */
#define FLASH_DEMO_ADDR         ((uint32_t)0x08010000)   /* Sector4，Bootloader(0~0x10000)与APP分区(0x20000~0x80000)之间的空闲区 */
#define FLASH_DEMO_DATA         ((uint32_t)0x12345678)

/*******************************************************************
** 函数名	: Flash_Get_Sector
** 函数描述	: 根据FLASH地址计算所在扇区号（参考野火例程GetSector）
**			F407ZG共12个扇区：0~3为16KB，4为64KB，5~11为128KB
** 参数		: [in] addr: FLASH地址（0x08000000 ~ 0x080FFFFF）
** 返回		: 扇区号（FLASH_SECTOR_0 ~ FLASH_SECTOR_11）
********************************************************************/
static uint32_t Flash_Get_Sector(uint32_t addr)
{
	uint32_t sector = FLASH_SECTOR_0;

	if      (addr < ADDR_FLASH_SECTOR_1)  sector = FLASH_SECTOR_0;
	else if (addr < ADDR_FLASH_SECTOR_2)  sector = FLASH_SECTOR_1;
	else if (addr < ADDR_FLASH_SECTOR_3)  sector = FLASH_SECTOR_2;
	else if (addr < ADDR_FLASH_SECTOR_4)  sector = FLASH_SECTOR_3;
	else if (addr < ADDR_FLASH_SECTOR_5)  sector = FLASH_SECTOR_4;
	else if (addr < ADDR_FLASH_SECTOR_6)  sector = FLASH_SECTOR_5;
	else if (addr < ADDR_FLASH_SECTOR_7)  sector = FLASH_SECTOR_6;
	else if (addr < ADDR_FLASH_SECTOR_8)  sector = FLASH_SECTOR_7;
	else if (addr < ADDR_FLASH_SECTOR_9)  sector = FLASH_SECTOR_8;
	else if (addr < ADDR_FLASH_SECTOR_10) sector = FLASH_SECTOR_9;
	else if (addr < ADDR_FLASH_SECTOR_11) sector = FLASH_SECTOR_10;
	else                                  sector = FLASH_SECTOR_11;

	return sector;
}

/*******************************************************************
** 函数名	: Flash_Write_Word
** 函数描述	: 向指定地址写入一个32位字（对应野火例程FLASH_ProgramWord）
**			FLASH只能把1写成0，目标位置必须已擦除(0xFFFFFFFF)
**			或新值与旧值相同，否则写入出错
** 参数		: [in] addr: 目标FLASH地址，需4字节对齐
**			: [in] data: 要写入的32位数据
** 返回		: 0=写入成功，1=写入失败
********************************************************************/
uint8_t Flash_Write_Word(uint32_t addr, uint32_t data)
{
	uint8_t ret = 0;

	/* 解锁FLASH，使能控制寄存器访问（对应野火 FLASH_Unlock） */
	HAL_FLASH_Unlock();

	/* 清除所有FLASH错误/结束标志位（对应野火 FLASH_ClearFlag） */
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                       FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

	/* 以字(32位)为单位编程，供电2.7~3.6V用VOLTAGE_RANGE_3 */
	if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data) != HAL_OK)
	{
		ret = 1;
	}

	/* 重新上锁，防止误操作 */
	HAL_FLASH_Lock();

	return ret;
}

/*******************************************************************
** 函数名	: Flash_Read_Word
** 函数描述	: 从指定地址读取一个32位字，内部FLASH可直接按指针读取
** 参数		: [in] addr: 源FLASH地址，需4字节对齐
** 返回		: 读到的32位数据
********************************************************************/
uint32_t Flash_Read_Word(uint32_t addr)
{
	return *(__IO uint32_t *)addr;
}

/*******************************************************************
** 函数名	: Flash_Erase_Sector_By_Addr
** 函数描述	: 擦除指定地址所在的扇区（对应野火例程FLASH_EraseSector）。
**			警告：扇区擦除会把整个扇区内容全部清为0xFFFFFFFF，
**			严禁对Bootloader区(Sector0~3)和APP分区(Sector5~7)调用；
**			备份分区(Sector8~10)升级时可整体擦除，参数存储建议
**			使用Sector4或Sector11
** 参数		: [in] addr: 扇区内任意地址
** 返回		: 0=擦除成功，1=擦除失败
********************************************************************/
uint8_t Flash_Erase_Sector_By_Addr(uint32_t addr)
{
	uint8_t ret = 0;
	uint32_t sector_error = 0;
	FLASH_EraseInitTypeDef erase_init;

	erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;      /* 按扇区擦除 */
	erase_init.Sector = Flash_Get_Sector(addr);          /* 地址换算成扇区号 */
	erase_init.NbSectors = 1;                            /* 只擦除1个扇区 */
	erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;     /* 2.7~3.6V，字编程 */

	HAL_FLASH_Unlock();

	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                       FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

	if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
	{
		ret = 1;
	}

	HAL_FLASH_Lock();

	return ret;
}

/*******************************************************************
** 函数名	: Flash_Write_Read_Demo
** 函数描述	: 内部FLASH读写演示：向0x08010000(Sector4)写入0x12345678后读回打印。
**			处理流程与野火例程一致：解锁->清标志->擦除->编程->读回校验。
**			该地址位于Bootloader与APP分区之间的空闲Sector4，擦除整个
**			Sector4(64KB)不会影响Bootloader(Sector0~3)和APP(Sector5~7)
** 参数		: 无
** 返回		: 无
********************************************************************/
void Flash_Write_Read_Demo(void)
{
	uint32_t cur_value = Flash_Read_Word(FLASH_DEMO_ADDR);
	uint32_t read_value = 0;

	printf("[FLASH] addr=0x%08X, current=0x%08X\r\n",
	       (unsigned int)FLASH_DEMO_ADDR, (unsigned int)cur_value);

	if (cur_value != FLASH_DEMO_DATA)
	{
		if (cur_value != 0xFFFFFFFF)
		{
			/* 非擦除态：先擦除所在扇区才能写入新值 */
			printf("[FLASH] erasing sector...\r\n");
			if (Flash_Erase_Sector_By_Addr(FLASH_DEMO_ADDR) != 0)
			{
				printf("[FLASH] erase FAILED\r\n");
				return;
			}
		}

		if (Flash_Write_Word(FLASH_DEMO_ADDR, FLASH_DEMO_DATA) == 0)
		{
			printf("[FLASH] write 0x%08X OK\r\n", (unsigned int)FLASH_DEMO_DATA);
		}
		else
		{
			printf("[FLASH] write FAILED\r\n");
			return;
		}
	}
	else
	{
		/* 掉电后重跑：数据已存在，无需重复写 */
		printf("[FLASH] data already exists, skip write\r\n");
	}

	/* 读回并校验 */
	read_value = Flash_Read_Word(FLASH_DEMO_ADDR);
	printf("[FLASH] read back=0x%08X, %s\r\n",
	       (unsigned int)read_value,
	       (read_value == FLASH_DEMO_DATA) ? "verify OK" : "verify FAILED");
}

/*********************************************END OF FILE**********************/
