/*
********************************************************************************
* Module: Firmware Boot Upgrade
* Description: Bootloader firmware copy and upgrade logic
********************************************************************************
*/

#include "fw_boot.h"
#include "stm32f4xx_hal_flash.h"
#include <string.h>
#include <stdio.h>

/* Function pointer type for jump to application */
typedef void (*pFunction)(void);

/* Debug printf - enable to see detailed copy progress */
#define FW_BOOT_DEBUG 1
#if FW_BOOT_DEBUG
  #define fw_boot_printf(...) printf(__VA_ARGS__)
#else
  #define fw_boot_printf(...) /* nothing */
#endif

/*
********************************************************************************
* Private Constants
********************************************************************************
*/

/* CRC32 lookup table (reflected polynomial 0xEDB88320) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

/*
********************************************************************************
* Private Function Prototypes
********************************************************************************
*/
static uint32_t fw_boot_get_sector_number(uint32_t address);
static uint32_t fw_boot_get_sector_size(uint32_t sector);

/*
********************************************************************************
* Public Functions
********************************************************************************
*/

/*******************************************************************
** Function Name  : FW_BOOT_IsUpdateRequired
** Description    : Check if firmware upgrade is required
** Parameters     : None
** Return         : 1 if upgrade required, 0 otherwise
********************************************************************/
uint8_t FW_BOOT_IsUpdateRequired(void)
{
    uint32_t magic = *(__IO uint32_t*)(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_MAGIC_OFFSET);

    return (magic == UPGRADE_FLAG_MAGIC) ? 1 : 0;
}

/*******************************************************************
** Function Name  : FW_BOOT_CopyFirmware
** Description    : Copy firmware from backup to application area
** Parameters     : None
** Return         : FW_BOOT_RESULT_E
********************************************************************/
FW_BOOT_RESULT_E FW_BOOT_CopyFirmware(void)
{
    uint32_t firmware_size;
    uint32_t firmware_crc32;
    uint32_t i;
    uint32_t *src_addr;
    uint32_t *dst_addr;
    FW_BOOT_RESULT_E result;

    /* Read upgrade flag */
    firmware_size = *(__IO uint32_t*)(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_SIZE_OFFSET);
    firmware_crc32 = *(__IO uint32_t*)(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_CRC_OFFSET);

    fw_boot_printf("[FW_BOOT] Flag: size=%lu, crc=0x%08lX\r\n", 
                   (unsigned long)firmware_size, (unsigned long)firmware_crc32);

    /* Validate firmware size */
    if (firmware_size == 0 || firmware_size > (FLASH_APP_END_ADDR - FLASH_APP_START_ADDR))
    {
        fw_boot_printf("[FW_BOOT] ERROR: Invalid size %lu (max=%lu)\r\n",
                       (unsigned long)firmware_size,
                       (unsigned long)(FLASH_APP_END_ADDR - FLASH_APP_START_ADDR));
        return FW_BOOT_RESULT_ERROR;
    }

    /* Calculate CRC32 of backup firmware */
    uint32_t calc_crc = FW_BOOT_CalculateCRC32((uint8_t*)FLASH_BACKUP_START_ADDR, firmware_size);

    fw_boot_printf("[FW_BOOT] CRC: calc=0x%08lX, expected=0x%08lX\r\n",
                   (unsigned long)calc_crc, (unsigned long)firmware_crc32);

    /* Verify CRC32 */
    if (calc_crc != firmware_crc32)
    {
        fw_boot_printf("[FW_BOOT] ERROR: CRC mismatch!\r\n");
        fw_boot_printf("[FW_BOOT] Dumping first 64 bytes of backup:\r\n");
        FW_BOOT_DumpBackupData(64);
        return FW_BOOT_RESULT_ERROR_CRC_MISMATCH;
    }

    fw_boot_printf("[FW_BOOT] CRC verified OK, erasing sectors 5-7...\r\n");

    /* Erase application sectors (5, 6, 7) */
    for (i = 5; i <= 7; i++)
    {
        result = FW_BOOT_FlashEraseSector(i);
        if (result != FW_BOOT_RESULT_SUCCESS)
        {
            fw_boot_printf("[FW_BOOT] ERROR: Erase sector %d failed (%d)\r\n", (int)i, (int)result);
            return result;
        }
    }

    fw_boot_printf("[FW_BOOT] Erase done, copying %lu bytes...\r\n", (unsigned long)firmware_size);

    /* Copy firmware word by word */
    src_addr = (uint32_t*)FLASH_BACKUP_START_ADDR;
    dst_addr = (uint32_t*)FLASH_APP_START_ADDR;

    for (i = 0; i < firmware_size; i += 4)
    {
        result = FW_BOOT_FlashWriteWord((uint32_t)dst_addr, *src_addr);
        if (result != FW_BOOT_RESULT_SUCCESS)
        {
            fw_boot_printf("[FW_BOOT] ERROR: Write failed at offset %lu\r\n", (unsigned long)i);
            return result;
        }
        src_addr++;
        dst_addr++;
    }

    fw_boot_printf("[FW_BOOT] Copy done, clearing flag...\r\n");

    /* Clear upgrade flag */
    result = FW_BOOT_ClearUpgradeFlag();
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        fw_boot_printf("[FW_BOOT] ERROR: Clear flag failed (%d)\r\n", (int)result);
        return result;
    }

    fw_boot_printf("[FW_BOOT] Firmware upgrade SUCCESS!\r\n");
    return FW_BOOT_RESULT_SUCCESS;
}

/*******************************************************************
** Function Name  : FW_BOOT_JumpToApp
** Description    : Jump to application code
** Parameters     : None
** Return         : None (never returns)
********************************************************************/
void FW_BOOT_JumpToApp(void)
{
    uint32_t jump_address;
    pFunction jump_to_app;

    /* Check if application exists */
    if (((*(__IO uint32_t*)FLASH_APP_START_ADDR) & 0x2FFE0000) == 0x20000000)
    {
        /* Disable all interrupts */
        __disable_irq();

        /* Disable SysTick */
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL = 0;

        /* Clear all pending interrupts */
        NVIC->ICER[0] = 0xFFFFFFFF;
        NVIC->ICER[1] = 0xFFFFFFFF;
        NVIC->ICER[2] = 0xFFFFFFFF;
        NVIC->ICPR[0] = 0xFFFFFFFF;
        NVIC->ICPR[1] = 0xFFFFFFFF;
        NVIC->ICPR[2] = 0xFFFFFFFF;

        /* Set vector table to application area */
        SCB->VTOR = FLASH_APP_START_ADDR;

        /* Get application reset handler address */
        jump_address = *(__IO uint32_t*)(FLASH_APP_START_ADDR + 4);
        jump_to_app = (pFunction)jump_address;

        /* Set MSP to application stack pointer */
        __set_MSP(*(__IO uint32_t*)FLASH_APP_START_ADDR);

        /* Enable interrupts */
        __enable_irq();

        /* Jump to application */
        jump_to_app();
    }
}

/*******************************************************************
** Function Name  : FW_BOOT_ClearUpgradeFlag
** Description    : Clear upgrade flag in flash
** Parameters     : None
** Return         : FW_BOOT_RESULT_E
********************************************************************/
FW_BOOT_RESULT_E FW_BOOT_ClearUpgradeFlag(void)
{
    FW_BOOT_RESULT_E result;
    uint32_t sector_number;

    /* Get sector number for flag area */
    sector_number = fw_boot_get_sector_number(UPGRADE_FLAG_ADDR);

    /* Erase sector */
    result = FW_BOOT_FlashEraseSector(sector_number);
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        return result;
    }

    return FW_BOOT_RESULT_SUCCESS;
}

/*******************************************************************
** Function Name  : FW_BOOT_SetUpgradeFlag
** Description    : Set upgrade flag in flash
** Parameters     : [in] firmware_size: firmware size in bytes
**                  [in] firmware_crc32: firmware CRC32
** Return         : FW_BOOT_RESULT_E
********************************************************************/
FW_BOOT_RESULT_E FW_BOOT_SetUpgradeFlag(uint32_t firmware_size, uint32_t firmware_crc32)
{
    FW_BOOT_RESULT_E result;
    uint32_t sector_number;

    /* Get sector number for flag area */
    sector_number = fw_boot_get_sector_number(UPGRADE_FLAG_ADDR);

    /* Erase sector first */
    result = FW_BOOT_FlashEraseSector(sector_number);
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        return result;
    }

    /* Write magic number */
    result = FW_BOOT_FlashWriteWord(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_MAGIC_OFFSET, UPGRADE_FLAG_MAGIC);
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        return result;
    }

    /* Write firmware size */
    result = FW_BOOT_FlashWriteWord(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_SIZE_OFFSET, firmware_size);
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        return result;
    }

    /* Write firmware CRC32 */
    result = FW_BOOT_FlashWriteWord(UPGRADE_FLAG_ADDR + UPGRADE_FLAG_CRC_OFFSET, firmware_crc32);
    if (result != FW_BOOT_RESULT_SUCCESS)
    {
        return result;
    }

    return FW_BOOT_RESULT_SUCCESS;
}

/*******************************************************************
** Function Name  : FW_BOOT_CalculateCRC32
** Description    : Calculate CRC32 of data block
** Parameters     : [in] data: pointer to data
**                  [in] length: data length in bytes
** Return         : CRC32 value
********************************************************************/
uint32_t FW_BOOT_CalculateCRC32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;

    for (i = 0; i < length; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

/*******************************************************************
** Function Name  : FW_BOOT_DumpBackupData
** Description    : Dump first N bytes of backup area for debugging
** Parameters     : [in] num_bytes: number of bytes to dump (max 64)
** Return         : None
********************************************************************/
void FW_BOOT_DumpBackupData(uint32_t num_bytes)
{
    uint32_t i;
    uint8_t *backup_addr = (uint8_t*)FLASH_BACKUP_START_ADDR;
    
    if (num_bytes > 64) num_bytes = 64;
    
    fw_boot_printf("[FW_BOOT] Backup area @0x%08lX:\r\n", (unsigned long)FLASH_BACKUP_START_ADDR);
    for (i = 0; i < num_bytes; i += 16) {
        fw_boot_printf("  %08lX: ", (unsigned long)(FLASH_BACKUP_START_ADDR + i));
        uint32_t j;
        for (j = 0; j < 16 && (i + j) < num_bytes; j++) {
            fw_boot_printf("%02X ", backup_addr[i + j]);
        }
        fw_boot_printf("\r\n");
    }
}

/*******************************************************************
** Function Name  : FW_BOOT_FlashWriteWord
** Description    : Write a word to flash
** Parameters     : [in] addr: flash address
**                  [in] data: word to write
** Return         : FW_BOOT_RESULT_E
********************************************************************/
FW_BOOT_RESULT_E FW_BOOT_FlashWriteWord(uint32_t addr, uint32_t data)
{
    HAL_StatusTypeDef status;

    /* Unlock flash */
    HAL_FLASH_Unlock();

    /* Clear pending flags */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* Program word */
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);

    /* Lock flash */
    HAL_FLASH_Lock();

    if (status != HAL_OK)
    {
        return FW_BOOT_RESULT_ERROR_FLASH_WRITE;
    }

    return FW_BOOT_RESULT_SUCCESS;
}

/*******************************************************************
** Function Name  : FW_BOOT_FlashEraseSector
** Description    : Erase a flash sector
** Parameters     : [in] sector: sector number (0-11)
** Return         : FW_BOOT_RESULT_E
********************************************************************/
FW_BOOT_RESULT_E FW_BOOT_FlashEraseSector(uint8_t sector)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;

    /* Validate sector number */
    if (sector > 11)
    {
        return FW_BOOT_RESULT_ERROR_INVALID_ADDR;
    }

    /* Unlock flash */
    HAL_FLASH_Unlock();

    /* Clear pending flags */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* Configure erase */
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = sector;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    /* Erase sector */
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    /* Lock flash */
    HAL_FLASH_Lock();

    if (status != HAL_OK)
    {
        return FW_BOOT_RESULT_ERROR_FLASH_ERASE;
    }

    return FW_BOOT_RESULT_SUCCESS;
}

/*
********************************************************************************
* Private Functions
********************************************************************************
*/

/*******************************************************************
** Function Name  : fw_boot_get_sector_number
** Description    : Get sector number for a given address
** Parameters     : [in] address: flash address
** Return         : Sector number (0-11)
********************************************************************/
static uint32_t fw_boot_get_sector_number(uint32_t address)
{
    if (address < ADDR_FLASH_SECTOR_1) return 0;
    else if (address < ADDR_FLASH_SECTOR_2) return 1;
    else if (address < ADDR_FLASH_SECTOR_3) return 2;
    else if (address < ADDR_FLASH_SECTOR_4) return 3;
    else if (address < ADDR_FLASH_SECTOR_5) return 4;
    else if (address < ADDR_FLASH_SECTOR_6) return 5;
    else if (address < ADDR_FLASH_SECTOR_7) return 6;
    else if (address < ADDR_FLASH_SECTOR_8) return 7;
    else if (address < ADDR_FLASH_SECTOR_9) return 8;
    else if (address < ADDR_FLASH_SECTOR_10) return 9;
    else if (address < ADDR_FLASH_SECTOR_11) return 10;
    else return 11;
}

/********************************END OF FILE****************************/
