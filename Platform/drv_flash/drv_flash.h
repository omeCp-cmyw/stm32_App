#ifndef __DRV_FLASH_H
#define	__DRV_FLASH_H

#include "stm32f4xx.h"
#include <stdint.h>

/* STM32F407ZG 内部FLASH扇区基地址（1MB：0x08000000 ~ 0x080FFFFF） */
#define ADDR_FLASH_SECTOR_0     ((uint32_t)0x08000000) /* Sector 0,  16 KB  */
#define ADDR_FLASH_SECTOR_1     ((uint32_t)0x08004000) /* Sector 1,  16 KB  */
#define ADDR_FLASH_SECTOR_2     ((uint32_t)0x08008000) /* Sector 2,  16 KB  */
#define ADDR_FLASH_SECTOR_3     ((uint32_t)0x0800C000) /* Sector 3,  16 KB  */
#define ADDR_FLASH_SECTOR_4     ((uint32_t)0x08010000) /* Sector 4,  64 KB  */
#define ADDR_FLASH_SECTOR_5     ((uint32_t)0x08020000) /* Sector 5,  128 KB */
#define ADDR_FLASH_SECTOR_6     ((uint32_t)0x08040000) /* Sector 6,  128 KB */
#define ADDR_FLASH_SECTOR_7     ((uint32_t)0x08060000) /* Sector 7,  128 KB */
#define ADDR_FLASH_SECTOR_8     ((uint32_t)0x08080000) /* Sector 8,  128 KB */
#define ADDR_FLASH_SECTOR_9     ((uint32_t)0x080A0000) /* Sector 9,  128 KB */
#define ADDR_FLASH_SECTOR_10    ((uint32_t)0x080C0000) /* Sector 10, 128 KB */
#define ADDR_FLASH_SECTOR_11    ((uint32_t)0x080E0000) /* Sector 11, 128 KB */
#define FLASH_END_ADDR          ((uint32_t)0x08100000)

/* FLASH 分区布局（与Bootloader fw_boot.h约定一致，严禁越界擦写）：
 * 0x08000000 ~ 0x08010000  Bootloader区（Sector 0~3，64KB）
 * 0x08010000 ~ 0x08020000  升级标志区（Sector 4，64KB）
 * 0x08020000 ~ 0x08080000  APP分区（Sector 5~7，384KB）
 * 0x08080000 ~ 0x080E0000  APP备份分区（Sector 8~10，384KB，升级备份用，可整体擦除）
 * 0x080E0000 ~ 0x08100000  Sector 11（128KB，预留）
 */
#define FLASH_BOOT_START_ADDR   ADDR_FLASH_SECTOR_0      /* Bootloader起始 */
#define FLASH_APP_START_ADDR    ADDR_FLASH_SECTOR_5      /* APP起始（链接地址） */
#define FLASH_APP_END_ADDR      ADDR_FLASH_SECTOR_8      /* APP分区结束（不含） */
#define FLASH_APP_BACKUP_START  ADDR_FLASH_SECTOR_8      /* APP备份分区起始 */
#define FLASH_APP_BACKUP_END    ADDR_FLASH_SECTOR_11     /* APP备份分区结束（不含） */

uint8_t Flash_Write_Word(uint32_t addr, uint32_t data);
uint32_t Flash_Read_Word(uint32_t addr);
uint8_t Flash_Erase_Sector_By_Addr(uint32_t addr);

#endif /* __DRV_FLASH_H */
