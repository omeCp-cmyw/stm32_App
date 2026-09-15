/********************************************************************************
**
** 文件名:     bsp_ap3216c.c
** 版权所有:   无
** 文件描述:   该模块主要实现AP3216C光照三合一传感器驱动（I2C1硬件方式）
**
** 功能: 复位+全功能模式+ALS/PS/IR三路读取。
** 接线: I2C1, PB8=SCL, PB9=SDA, 器件7位地址0x1E。
**
*********************************************************************************/


#include "bsp_ap3216c.h"
#include "stm32f4xx_hal.h"
#include "../../Tools/dwt_delay/core_delay.h"

/* I2C1硬件引脚配置 */
#define AP3216C_I2C                 I2C1
#define AP3216C_I2C_CLK_ENABLE()    __HAL_RCC_I2C1_CLK_ENABLE()
#define AP3216C_SCL_PIN             GPIO_PIN_8      /* PB8 */
#define AP3216C_SDA_PIN             GPIO_PIN_9      /* PB9 */
#define AP3216C_GPIO_AF             GPIO_AF4_I2C1
#define AP3216C_I2C_SPEED           400000          /* 400kHz快速模式 */

/* 器件7位地址0x1E(8位写地址0x3C) */
#define AP3216C_ADDR                0x1E

/* 数据寄存器 */
#define AP3216C_SYS_CONFIG_REG      0x00    /* 系统配置(bit2:0工作模式) */
#define AP3216C_IR_DATA_L_REG       0x0A    /* 红外数据低字节 */
#define AP3216C_ALS_DATA_L_REG      0x0C    /* 环境光数据低字节 */
#define AP3216C_PS_DATA_L_REG       0x0E    /* 接近数据低字节 */
#define AP3216C_ALS_CONFIG_REG      0x10    /* ALS配置(bit5:4量程) */

/* 系统工作模式 */
#define AP3216C_MODE_POWER_DOWN     0x00    /* 掉电(默认) */
#define AP3216C_MODE_ALS            0x01    /* 仅环境光 */
#define AP3216C_MODE_PS             0x02    /* 仅接近+红外 */
#define AP3216C_MODE_ALS_AND_PS     0x03    /* 环境光+接近+红外(全功能) */
#define AP3216C_MODE_SW_RESET       0x04    /* 软件复位 */

/* ALS量程(bit5:4)与分辨率 */
#define AP3216C_ALS_RANGE_20661     0x00    /* 0.35 lux/count(默认) */
#define AP3216C_ALS_RANGE_5162      0x01    /* 0.0788 lux/count */
#define AP3216C_ALS_RANGE_1291      0x02    /* 0.0197 lux/count */
#define AP3216C_ALS_RANGE_323       0x03    /* 0.0049 lux/count */

static I2C_HandleTypeDef s_hi2c;

/*******************************************************************************
** 函数名称    ap3216c_write_reg
** 函数说明    写单字节寄存器
** 输入参数    reg: 寄存器地址
**             data: 写入值
** 输出参数    无
** 返回参数    无
*******************************************************************************/
static void ap3216c_write_reg(uint8_t reg, uint8_t data)
{
    (void)HAL_I2C_Mem_Write(&s_hi2c, AP3216C_ADDR << 1, reg,
                            I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

/*******************************************************************************
** 函数名称    ap3216c_read_regs
** 函数说明    读寄存器(可连续读len字节, 地址自增)
** 输入参数    reg: 起始寄存器地址
**             len: 读取字节数
**             buf: 数据缓冲
** 输出参数    buf: 读回数据
** 返回参数    无
*******************************************************************************/
static void ap3216c_read_regs(uint8_t reg, uint8_t len, uint8_t *buf)
{
    (void)HAL_I2C_Mem_Read(&s_hi2c, AP3216C_ADDR << 1, reg,
                           I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

/*******************************************************************************
** 函数名称    ap3216c_read_low_high
** 函数说明    读16位数据寄存器(低字节在前, 两次单字节读)
** 输入参数    reg: 低字节寄存器地址
** 输出参数    无
** 返回参数    16位数据
*******************************************************************************/
static uint32_t ap3216c_read_low_high(uint8_t reg)
{
    uint8_t lo = 0;
    uint8_t hi = 0;

    ap3216c_read_regs(reg, 1, &lo);                 /* 读低字节 */
    ap3216c_read_regs((uint8_t)(reg + 1), 1, &hi);  /* 读高字节 */

    return (uint32_t)(lo | ((uint32_t)hi << 8));
}

/*******************************************************************************
** 函数名称    ap3216c_init
** 函数说明    初始化AP3216C(I2C1硬件配置+芯片复位+全功能模式)
** 输入参数    无
** 输出参数    无
** 返回参数    0: 成功, -1: 失败(I2C初始化异常或芯片无应答)
** 备注        AP3216C从掉电进入ALS模式后首次转换需约400ms积分时间,
**             模式设置后须等≥500ms再读, 否则首读为0
*******************************************************************************/
int ap3216c_init(void)
{
    GPIO_InitTypeDef gio;
    uint8_t mode;
    int retry;

    /* I2C1引脚: PB8=SCL, PB9=SDA, 复用开漏+上拉 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gio.Pin = AP3216C_SCL_PIN | AP3216C_SDA_PIN;
    gio.Mode = GPIO_MODE_AF_OD;
    gio.Pull = GPIO_PULLUP;
    gio.Speed = GPIO_SPEED_FAST;
    gio.Alternate = AP3216C_GPIO_AF;
    HAL_GPIO_Init(GPIOB, &gio);

    /* I2C1: 400kHz快速模式, 7位地址 */
    AP3216C_I2C_CLK_ENABLE();
    /* I2C1外设复位(确保总线上电干净) */
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    s_hi2c.Instance = AP3216C_I2C;
    s_hi2c.Init.ClockSpeed = AP3216C_I2C_SPEED;
    s_hi2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_hi2c.Init.OwnAddress1 = 0x00;
    s_hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c.Init.OwnAddress2 = 0x00;
    s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&s_hi2c) != HAL_OK) {
        return -1;
    }

    /* 软件复位+全功能模式配置, 失败重试3次:
     * MCU与传感器模块同时上电时芯片启动稳定需要时间, 首次通信可能失败 */
    for (retry = 0; retry < 3; retry++) {
        ap3216c_write_reg(AP3216C_SYS_CONFIG_REG, AP3216C_MODE_SW_RESET);
        Delay_ms(100);

        ap3216c_write_reg(AP3216C_SYS_CONFIG_REG, AP3216C_MODE_ALS_AND_PS);
        Delay_ms(100);

        /* 回读系统配置寄存器校验通信(芯片缺失/未稳定时重试) */
        ap3216c_read_regs(AP3216C_SYS_CONFIG_REG, 1, &mode);
        if ((mode & 0x07) == AP3216C_MODE_ALS_AND_PS) {
            break;
        }
        Delay_ms(100);
    }
    if (retry >= 3) {
        return -1;
    }

    /* 等待首次ALS转换完成(默认积分时间约400ms),
     * 首读在init后500ms定时任务中, 此处对齐以免首读为0 */
    Delay_ms(500);

    return 0;
}

/*******************************************************************************
** 函数名称    ap3216c_read_ambient_light
** 函数说明    读环境光强度, 按当前量程分辨率换算为lux
** 输入参数    无
** 输出参数    无
** 返回参数    光照强度(lux)
*******************************************************************************/
float ap3216c_read_ambient_light(void)
{
    uint32_t raw;
    uint8_t als_cfg;
    float res = 0.35f;      /* 默认量程0: 0.35lux/count */

    raw = ap3216c_read_low_high(AP3216C_ALS_DATA_L_REG);

    /* 读取当前量程, 按分辨率换算 */
    ap3216c_read_regs(AP3216C_ALS_CONFIG_REG, 1, &als_cfg);
    als_cfg = (als_cfg >> 4) & 0x03;
    if (als_cfg == AP3216C_ALS_RANGE_5162) {
        res = 0.0788f;
    } else if (als_cfg == AP3216C_ALS_RANGE_1291) {
        res = 0.0197f;
    } else if (als_cfg == AP3216C_ALS_RANGE_323) {
        res = 0.0049f;
    }

    return (float)raw * res;
}

/*******************************************************************************
** 函数名称    ap3216c_read_ps_data
** 函数说明    读接近数据(10bit有效值+bit15远近状态位)
** 输入参数    无
** 输出参数    无
** 返回参数    接近数据; IR过强导致PS无效时返回55555
*******************************************************************************/
uint16_t ap3216c_read_ps_data(void)
{
    uint32_t read_data;
    uint16_t proximity;

    read_data = ap3216c_read_low_high(AP3216C_PS_DATA_L_REG);

    /* IR太强, PS数据无效 */
    if (((read_data >> 6) & 0x01) || ((read_data >> 14) & 0x01)) {
        return 55555;
    }

    proximity = (uint16_t)((read_data & 0x000F) + (((read_data >> 8) & 0x3F) << 4));
    proximity |= (uint16_t)(read_data & 0x8000);    /* bit15: 1近 0远 */

    return proximity;
}

/*******************************************************************************
** 函数名称    ap3216c_read_ir_data
** 函数说明    读红外数据
** 输入参数    无
** 输出参数    无
** 返回参数    红外数据值
*******************************************************************************/
uint16_t ap3216c_read_ir_data(void)
{
    uint32_t read_data;

    read_data = ap3216c_read_low_high(AP3216C_IR_DATA_L_REG);

    return (uint16_t)((read_data & 0x0003) + ((read_data >> 8) & 0xFF));
}
