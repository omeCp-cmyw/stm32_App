#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "drv_uart.h"
#include "os_include.h"
#include "tool_loopbuf.h"

/*
********************************************************************************
* define config parameters
********************************************************************************
*/
#define _OPEN                   0x80

/* DMA暂存区与环形缓冲区上限（接收环缓冲2048：容纳Ymodem 1K包的1029字节） */
#define DMA_RX_LEN              1024
#define DMA_TX_LEN              512
#define DRV_UART_RX_BUF_MAX         2048
#define DRV_UART_TX_BUF_MAX         512

/* 各串口默认波特率与默认环缓冲参数（引脚/地址从drv_uart_reg注册表获取） */
#define UART_DEFAULT_BAUDRATE   115200
#define DRV_UART_RX_LEN_PRESET      2048     /* COM_0/COM_1接收环缓冲（容纳Ymodem 1K包） */
#define DRV_UART_RX_LEN_MINI        256      /* COM_2预留口接收环缓冲 */
#define DRV_UART_TX_LEN_PRESET      512

/*
********************************************************************************
* define struct
********************************************************************************
*/
/* 串口控制块 */
typedef struct {
    INT8U       status;
    INT8U       tx_mode;       /* 0空闲 1发送中 */
    INT8U       rx_idle;       /* 空闲帧结束标志：环缓冲中有一帧完整数据待读（参考野火rx_flag） */

    LOOP_BUF_T  r_round;       /* 接收环形缓冲 */
    LOOP_BUF_T  s_round;       /* 发送环形缓冲 */

    INT8U       p_dma_rx[DMA_RX_LEN];    /* DMA接收暂存区（仅DMA接收口使用） */
    INT8U       p_dma_tx[DMA_TX_LEN];    /* 中断发送暂存区 */

    DRV_UART_CFG_T  uartcfg;
} DRV_UART_T;

/*
********************************************************************************
* define module variants
********************************************************************************
*/
static DRV_UART_T s_uart[DRV_UART_COM_MAX];

/* 各串口收发环缓冲内存独立（多串口共享缓冲会互相抢占数据） */
static INT8U s_rx_mem[DRV_UART_COM_MAX][DRV_UART_RX_BUF_MAX];
static INT8U s_tx_mem[DRV_UART_COM_MAX][DRV_UART_TX_BUF_MAX];

/* 串口硬件句柄，由本模块统一管理：USART1带DMA，USART2/3中断收发 */
static DMA_HandleTypeDef  hdma_usart1_rx;
static DMA_HandleTypeDef  hdma_usart1_tx;
static UART_HandleTypeDef Uart1Handle;
static UART_HandleTypeDef Uart2Handle;
static UART_HandleTypeDef Uart3Handle;

/* printf静音标志，升级期间置1避免日志混入升级串口（见DRV_UART_MutePrint） */
static INT8U s_print_mute;

/*******************************************************************
** 函数名	: fputc
** 函数描述	: 重定向printf：走发送环缓冲中断式发出，阻塞直发会与
**			: WriteBlock争抢HAL发送状态机（HAL_BUSY）导致丢字。
** 参数		: [in] ch: 待输出字符
**			: [in] f:  文件指针，未使用
** 返回		: 原字符
********************************************************************/
int fputc(int ch, FILE *f)
{
    if (s_print_mute == 0) {
        DRV_UART_WriteChar(DRV_UART_COM_0, (INT8U)ch);
    }
    return (ch);
}

/*******************************************************************
** 函数名	: DRV_UART_MutePrint
** 函数描述	: 开关printf输出：升级期间静音避免日志混入升级串口。
** 参数		: [in] mute: 1静音，0恢复
** 返回		: 无
********************************************************************/
void DRV_UART_MutePrint(INT8U mute)
{
    s_print_mute = mute;
}

/*
********************************************************************************
* 硬件初始化（引脚、地址、DMA通道均从drv_uart_reg注册表获取）
********************************************************************************
*/
/*******************************************************************
** 函数名	: uart_clk_enable
** 函数描述	: 使能串口外设时钟，按基地址判断挂在APB1还是APB2总线。
** 参数		: [in] uart_base: 串口寄存器基地址
** 返回		: 无
********************************************************************/
static void uart_clk_enable(INT32U uart_base)
{
    if (uart_base >= APB2PERIPH_BASE) {
        RCC->APB2ENR |= (1UL << ((uart_base - APB2PERIPH_BASE) >> 10));
    } else {
        RCC->APB1ENR |= (1UL << ((uart_base - APB1PERIPH_BASE) >> 10));
    }
}

/*******************************************************************
** 函数名	: gpio_clk_enable
** 函数描述	: 使能GPIO端口时钟，按基地址偏移确定AHB1ENR位号。
** 参数		: [in] gpio_base: GPIO端口基地址
** 返回		: 无
********************************************************************/
static void gpio_clk_enable(INT32U gpio_base)
{
    RCC->AHB1ENR |= (1UL << ((gpio_base - GPIOA_BASE) >> 10));
}

/*******************************************************************
** 函数名	: uart_pin_init
** 函数描述	: 串口TX/RX引脚初始化，引脚与复用号取自注册表。
** 参数		: [in] pinfo: 注册表项指针
** 返回		: 无
********************************************************************/
static void uart_pin_init(const DRV_UART_TBL_T *pinfo)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_TypeDef *port = (GPIO_TypeDef *)pinfo->gpio_port;

    /* 使能GPIO时钟 */
    gpio_clk_enable(pinfo->gpio_port);

    /* GPIO初始化：推挽复用、上拉、高速 */
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    /* 配置Tx引脚为复用功能 */
    GPIO_InitStruct.Pin       = pinfo->tx_pin;
    GPIO_InitStruct.Alternate = pinfo->tx_af;
    HAL_GPIO_Init(port, &GPIO_InitStruct);

    /* 配置Rx引脚为复用功能 */
    GPIO_InitStruct.Pin       = pinfo->rx_pin;
    GPIO_InitStruct.Alternate = pinfo->rx_af;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

/*******************************************************************
** 函数名	: uart_dma_init
** 函数描述	: 串口DMA初始化（TX和RX两路，NORMAL模式），流与通道取自注册表。
** 参数		: [in] pinfo: 注册表项指针
** 返回		: 无
********************************************************************/
static void uart_dma_init(const DRV_UART_TBL_T *pinfo)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* RX通道 */
    hdma_usart1_rx.Instance                 = (DMA_Stream_TypeDef *)pinfo->dma_rx_stream;
    hdma_usart1_rx.Init.Channel             = pinfo->dma_rx_channel;
    hdma_usart1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode                = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_usart1_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_usart1_rx);

    __HAL_LINKDMA(&Uart1Handle, hdmarx, hdma_usart1_rx);

    /* TX通道 */
    hdma_usart1_tx.Instance                 = (DMA_Stream_TypeDef *)pinfo->dma_tx_stream;
    hdma_usart1_tx.Init.Channel             = pinfo->dma_tx_channel;
    hdma_usart1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode                = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_usart1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_usart1_tx);

    __HAL_LINKDMA(&Uart1Handle, hdmatx, hdma_usart1_tx);
}

static UART_HandleTypeDef *uart_get_handle(INT8U com);
static INT8U uart_rx_by_dma(INT8U com);
static void uart_rx_it_enable(INT8U com);

/*******************************************************************
** 函数名	: uart_hw_init
** 函数描述	: 通用串口硬件初始化，115200 8-N-1：时钟/引脚/工作模式从注册表取，
**			: DMA接收口追加DMA通道初始化，中断接收口在此使能RXNE/IDLE中断。
** 参数		: [in] com:   串口号，见DRV_UART_COM_E
**			: [in] irqn:  串口中断号（USARTx_IRQn）
** 返回		: 无
********************************************************************/
static void uart_hw_init(INT8U com, IRQn_Type irqn)
{
    const DRV_UART_TBL_T *pinfo = DRV_UART_GetRegTblInfo(com);
    UART_HandleTypeDef *huart = uart_get_handle(com);

    if ((pinfo == 0) || (pinfo->enable == 0) || (huart == 0)) {
        return;
    }

    /* 使能串口时钟 */
    uart_clk_enable(pinfo->uart_base);

    /* 引脚复用配置 */
    uart_pin_init(pinfo);

    /* 配置串口工作模式 */
    huart->Instance = (USART_TypeDef *)pinfo->uart_base;
    /* 波特率设置 */
    huart->Init.BaudRate   = UART_DEFAULT_BAUDRATE;
    /* 字长(数据位+校验位)：8 */
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    /* 停止位：1个停止位 */
    huart->Init.StopBits   = UART_STOPBITS_1;
    /* 校验位选择：无校验 */
    huart->Init.Parity     = UART_PARITY_NONE;
    /* 硬件流控制：不使用硬件流 */
    huart->Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    /* USART模式控制：同时使能接收和发送 */
    huart->Init.Mode       = UART_MODE_TX_RX;

    /* 完成串口初始化配置并使能 */
    HAL_UART_Init(huart);

    if (uart_rx_by_dma(com)) {
        /* DMA收发通道初始化（当前仅USART1，句柄内硬连接） */
        uart_dma_init(pinfo);

        HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

        HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
    } else {
        /* 中断接收：单独使能RXNE与IDLE中断（参考野火多串口例程） */
        uart_rx_it_enable(com);
    }

    /* 设置中断优先级，使能中断 */
    HAL_NVIC_SetPriority(irqn, 1, 0);
    HAL_NVIC_EnableIRQ(irqn);
}

/*******************************************************************
** 函数名	: uart1_hw_init
** 函数描述	: USART1硬件初始化，115200 8-N-1，含DMA和中断配置。
** 参数		: 无
** 返回		: 无
********************************************************************/
static void uart1_hw_init(void)
{
    uart_hw_init(DRV_UART_COM_0, USART1_IRQn);
}

/*******************************************************************
** 函数名	: uart_get_handle
** 函数描述	: 按串口号取硬件句柄。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 句柄指针，未注册返回0
********************************************************************/
static UART_HandleTypeDef *uart_get_handle(INT8U com)
{
    switch (com) {
    case DRV_UART_COM_0:
        return &Uart1Handle;
    case DRV_UART_COM_1:
        return &Uart2Handle;
    case DRV_UART_COM_2:
        return &Uart3Handle;
    default:
        return 0;
    }
}

/*******************************************************************
** 函数名	: uart_rx_by_dma
** 函数描述	: 判断串口接收方式：注册表登记了DMA流则DMA接收，否则中断接收。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 1为DMA接收，0为中断接收
********************************************************************/
static INT8U uart_rx_by_dma(INT8U com)
{
    const DRV_UART_TBL_T *pinfo = DRV_UART_GetRegTblInfo(com);

    return (pinfo != 0 && pinfo->dma_rx_stream != 0) ? 1 : 0;
}

/*******************************************************************
** 函数名	: uart_rx_it_enable
** 函数描述	: 中断接收使能：单独开RXNE与IDLE中断（参考野火多串口例程），
**			: ISR内逐字节写入接收环形缓冲，上层读取接口与DMA口一致。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 无
********************************************************************/
static void uart_rx_it_enable(INT8U com)
{
    UART_HandleTypeDef *huart = uart_get_handle(com);

    if (huart == 0) {
        return;
    }
    __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);                          /* 接收数据非空中断 */
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);                          /* 总线空闲中断 */
}

/*
********************************************************************************
* 内部接口
********************************************************************************
*/
/*******************************************************************
** 函数名	: uart_rx_dma_restart
** 函数描述	: 启动DMA+空闲中断接收到暂存区，收满或总线空闲时触发回调。
** 参数		: [in] com: 串口号
** 返回		: 无
********************************************************************/
static void uart_rx_dma_restart(INT8U com)
{
    if (com == DRV_UART_COM_0) {
        HAL_UARTEx_ReceiveToIdle_DMA(&Uart1Handle, s_uart[com].p_dma_rx, DMA_RX_LEN);
    }
}

/*******************************************************************
** 函数名	: uart_rx_isr
** 函数描述	: 中断接收口通用ISR：RXNE读DR逐字节写接收环形缓冲，
**			: IDLE读SR+DR清标志；其余标志（含发送完成）交HAL分发，
**			: 保证中断式发送的TxCplt回调正常续发。
**			: RXNE必须在调HAL_UART_IRQHandler前自行读掉：HAL未启动
**			: 中断接收（RxState非BUSY_RX），RXNE悬空会反复进中断。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 无
********************************************************************/
static void uart_rx_isr(INT8U com)
{
    UART_HandleTypeDef *huart = uart_get_handle(com);
    INT8U byte;

    if (huart == 0) {
        return;
    }

    /* RXNE：读DR即清除标志，字节直接写入接收环形缓冲 */
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE) != RESET) {
        byte = (INT8U)READ_REG(huart->Instance->DR);
        LP_WriteBlockLoopBuffer_INT(&s_uart[com].r_round, &byte, 1);   /* 缓冲满时丢弃本字节 */
    }

    /* IDLE：按手册读SR再读DR清除；置帧结束标志，上层凑齐一帧再整批读取（参考野火以空闲中断判一帧） */
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) != RESET) {
        volatile INT32U tmp;
        tmp = READ_REG(huart->Instance->SR);
        tmp = READ_REG(huart->Instance->DR);
        tmp = tmp;
        s_uart[com].rx_idle = 1;
    }

    /* 发送完成等其余中断交HAL处理 */
    HAL_UART_IRQHandler(huart);
}

/*******************************************************************
** 函数名	: uart_tx_kick
** 函数描述	: 发送环形缓冲有待发数据且总线空闲时，拷贝一段到暂存区用中断发出。
** 参数		: [in] com: 串口号
** 返回		: 无
********************************************************************/
static void uart_tx_kick(INT8U com)
{
    INT32U n;
    UART_HandleTypeDef *huart = uart_get_handle(com);

    if (huart == 0) {
        return;
    }

    OS_ENTER_CRITICAL();
    n = LP_UsedOfLoopBuffer_INT(&s_uart[com].s_round);
    if (s_uart[com].tx_mode != 0 || n == 0) {
        OS_EXIT_CRITICAL();
        return;
    }

    if (n > DMA_TX_LEN) {
        n = DMA_TX_LEN;
    }
    LP_ReadBlockLoopBuffer_INT(&s_uart[com].s_round, s_uart[com].p_dma_tx, n);
    s_uart[com].tx_mode = 1;
    OS_EXIT_CRITICAL();

    if (HAL_UART_Transmit_IT(huart, s_uart[com].p_dma_tx, (uint16_t)n) != HAL_OK) {
        s_uart[com].tx_mode = 0;                                       /* 发送启动失败，数据已出队无法恢复 */
    }
}

/*
********************************************************************************
* 对外接口
********************************************************************************
*/
/*******************************************************************
** 函数名	: DRV_UART_InitDrv
** 函数描述	: 初始化串口驱动，先完成硬件配置再按默认参数打开所有串口。
** 参数		: 无
** 返回		: 无
********************************************************************/
void DRV_UART_InitDrv(void)
{
    DRV_UART_CFG_T cfg;

    uart1_hw_init();                                                   /* USART1硬件初始化（DMA接收） */
    uart_hw_init(DRV_UART_COM_1, USART2_IRQn);                         /* USART2 RS232-U2（中断收发，预留） */
    uart_hw_init(DRV_UART_COM_2, USART3_IRQn);                         /* USART3 RS232-U3（中断收发，Ymodem升级通道） */
    memset(s_uart, 0, sizeof(s_uart));

    cfg.com     = DRV_UART_COM_0;
    cfg.baud    = 115200;
    cfg.parity  = DRV_UART_PARITY_NONE_YX;
    cfg.databit = DRV_UART_DATABIT_8;
    cfg.stopbit = DRV_UART_STOPBIT_1;
    cfg.rx_len  = DRV_UART_RX_LEN_PRESET;
    cfg.tx_len  = DRV_UART_TX_LEN_PRESET;
    DRV_UART_OpenUart(&cfg);

    cfg.com    = DRV_UART_COM_1;
    cfg.rx_len = DRV_UART_RX_LEN_PRESET;                                   /* Ymodem 1K包1029字节 */
    DRV_UART_OpenUart(&cfg);

    cfg.com    = DRV_UART_COM_2;
    cfg.rx_len = DRV_UART_RX_LEN_MINI;
    DRV_UART_OpenUart(&cfg);
}

/*******************************************************************
** 函数名	: DRV_UART_OpenUart
** 函数描述	: 打开串口并初始化缓冲区。
** 参数		: [in] cfg: 串口配置参数
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_OpenUart(DRV_UART_CFG_T *cfg)
{
    INT8U com;

    OS_ASSERT((cfg != 0), RETURN_FALSE);
    OS_ASSERT((cfg->com < DRV_UART_COM_MAX), RETURN_FALSE);
    OS_ASSERT((cfg->rx_len != 0 && cfg->rx_len <= DRV_UART_RX_BUF_MAX), RETURN_FALSE);
    OS_ASSERT((cfg->tx_len != 0 && cfg->tx_len <= DRV_UART_TX_BUF_MAX), RETURN_FALSE);

    com = (INT8U)cfg->com;

    /* 波特率与现有配置不一致时重配，避免中断打印失效 */
    {
        UART_HandleTypeDef *huart = uart_get_handle(com);
        if (huart != 0 && huart->Init.BaudRate != cfg->baud) {
            huart->Init.BaudRate = cfg->baud;
            HAL_UART_Init(huart);
        }
    }

    LP_InitLoopBuffer(&s_uart[com].r_round, s_rx_mem[com], cfg->rx_len);
    LP_InitLoopBuffer(&s_uart[com].s_round, s_tx_mem[com], cfg->tx_len);

    memcpy(&s_uart[com].uartcfg, cfg, sizeof(DRV_UART_CFG_T));
    s_uart[com].tx_mode = 0;
    s_uart[com].status  = _OPEN;

    if (uart_rx_by_dma(com)) {
        uart_rx_dma_restart(com);                                      /* DMA口启动DMA接收 */
    }

    return TRUE;
}

/*******************************************************************
** 函数名	: DRV_UART_CloseUart
** 函数描述	: 关闭串口，停止接收。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_CloseUart(INT8U com)
{
    OS_ASSERT((com < DRV_UART_COM_MAX), RETURN_FALSE);

    if ((s_uart[com].status & _OPEN) == 0) {
        return TRUE;
    }

    s_uart[com].status &= ~(_OPEN);
    {
        UART_HandleTypeDef *huart = uart_get_handle(com);
        if (huart != 0) {
            if (uart_rx_by_dma(com)) {
                HAL_UART_AbortReceive(huart);
            } else {
                __HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);            /* 中断口关接收中断 */
                __HAL_UART_DISABLE_IT(huart, UART_IT_IDLE);
            }
        }
    }
    return TRUE;
}

/*******************************************************************
** 函数名	: DRV_UART_ReadChar
** 函数描述	: 从接收环形缓冲读取一个字节。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 成功返回数据，失败返回-1。
********************************************************************/
INT32S DRV_UART_ReadChar(INT8U com)
{
    if (com >= DRV_UART_COM_MAX) {
        return -1;
    }

    if ((s_uart[com].status & _OPEN) == 0) {
        return -1;
    }

    return LP_ReadLoopBuffer(&s_uart[com].r_round);
}

/*******************************************************************
** 函数名	: DRV_UART_GetRecvBytes
** 函数描述	: 获取接收环形缓冲中已收字节数。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 已收字节数。
********************************************************************/
INT32U DRV_UART_GetRecvBytes(INT8U com)
{
    INT32U len;
    if (com >= DRV_UART_COM_MAX) {
        return 0;
    }

    if ((s_uart[com].status & _OPEN) == 0) {
        return 0;
    }

    len = LP_UsedOfLoopBuffer(&s_uart[com].r_round);
    return len;
}

/*******************************************************************
** 函数名	: DRV_UART_RxByteProcess
** 函数描述	: 接收数据处理入口：仅当收到完整一帧（总线空闲触发帧标志）才
**			: 从接收环形缓冲一次读出整帧，经日志串口（USART1）打印HEX+ASCII，
**			: 避免中断接收口逐字节到达被拆成多次打印；
**			: 串口3（COM_2）额外原路回显整帧用于链路验证。调试用。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 无
********************************************************************/
void DRV_UART_RxByteProcess(INT8U com)
{
    INT32U len;
    INT16U i;
    static INT8U buf[DMA_RX_LEN];

    if (com >= DRV_UART_COM_MAX) {
        return;
    }

    /* 未收到完整一帧不打印，数据留在环缓冲等待空闲中断凑帧 */
    if (s_uart[com].rx_idle == 0) {
        return;
    }
    s_uart[com].rx_idle = 0;

    len = DRV_UART_GetRecvBytes(com);
    if (len == 0) {
        return;
    }
    if (len > DMA_RX_LEN) {
        len = DMA_RX_LEN;                                              /* 超出部分下次处理 */
    }

    if (LP_ReadBlockLoopBuffer(&s_uart[com].r_round, buf, len) == FALSE) {
        return;
    }

    printf("[UART%u] received %u bytes: ", (unsigned int)(com + 1), (unsigned int)len);  /* COM下标+1对齐USART编号 */
    for (i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("| ");
    for (i = 0; i < len; i++) {
        /* 不可见字符用.代替 */
        printf("%c", (buf[i] >= 0x20 && buf[i] <= 0x7E) ? buf[i] : '.');
    }
    printf("\r\n");

    /* 串口3数据回显：整帧从原口发回发送方验证收发链路；
       注意COM_2同时是Ymodem升级通道，回显测试与升级会话不能并存（见main） */
    if (com == DRV_UART_COM_2) {
        DRV_UART_WriteBlock(com, buf, (INT16U)len+3);
    }
}

/*******************************************************************
** 函数名	: DRV_UART_WriteChar
** 函数描述	: 发送一个字节，写入发送环形缓冲区后由中断发出。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] data: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteChar(INT8U com, INT8U data)
{
    return DRV_UART_WriteBlock(com, &data, 1);
}

/*******************************************************************
** 函数名	: DRV_UART_WriteCharWait
** 函数描述	: 阻塞方式发送一个字节，等待发送完成。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] data: 字节数据
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteCharWait(INT8U com, INT8U data)
{
    UART_HandleTypeDef *huart = uart_get_handle(com);

    OS_ASSERT((com < DRV_UART_COM_MAX), RETURN_FALSE);
    OS_ASSERT((huart != 0), RETURN_FALSE);

    if (HAL_UART_Transmit(huart, &data, 1, 1000) != HAL_OK) {
        return FALSE;
    }
    return TRUE;
}

/*******************************************************************
** 函数名	: DRV_UART_WriteBlock
** 函数描述	: 发送一段数据，写入发送环形缓冲区后由中断发出。
** 参数		: [in] com:  串口号，见DRV_UART_COM_E
**			: [in] sptr: 数据指针
**			: [in] slen: 数据长度
** 返回		: 成功true，失败false
********************************************************************/
BOOLEAN DRV_UART_WriteBlock(INT8U com, INT8U *sptr, INT16U slen)
{
    BOOLEAN result;

    if ((sptr == 0) || (slen == 0)) {
        return FALSE;
    }

    if (com >= DRV_UART_COM_MAX) {
        return FALSE;
    }

    if ((s_uart[com].status & _OPEN) == 0) {
        return FALSE;
    }

    OS_ENTER_CRITICAL();
    result = LP_WriteBlockLoopBuffer_INT(&s_uart[com].s_round, sptr, slen);
    OS_EXIT_CRITICAL();

    if (result == FALSE) {
        return FALSE;                                                  /* 发送缓冲满 */
    }

    uart_tx_kick(com);
    return TRUE;
}

/*******************************************************************
** 函数名	: DRV_UART_LeftOfSendbuf
** 函数描述	: 获取发送缓冲区剩余空间。
** 参数		: [in] com: 串口号，见DRV_UART_COM_E
** 返回		: 剩余空间字节数
********************************************************************/
INT32U DRV_UART_LeftOfSendbuf(INT8U com)
{
    if (com >= DRV_UART_COM_MAX) {
        return 0;
    }

    if ((s_uart[com].status & _OPEN) == 0) {
        return 0;
    }

    return LP_LeftOfLoopBuffer(&s_uart[com].s_round);
}

/*
********************************************************************************
* HAL回调与中断入口（全局唯一，由本模块统一管理）
********************************************************************************
*/
/*******************************************************************
** 函数名	: HAL_UARTEx_RxEventCallback
** 函数描述	: DMA接收完成/总线空闲回调，把暂存区数据写入接收环形缓冲后
**			立即重启DMA接收，上层通过ReadChar/GetRecvBytes读取。
** 参数		: [in] huart: 串口句柄
**			: [in] Size:  本次接收字节数
** 返回		: 无
********************************************************************/
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    INT8U com = DRV_UART_COM_MAX;

    if (huart->Instance == USART1) {
        com = DRV_UART_COM_0;
    }
    if (com >= DRV_UART_COM_MAX) {
        return;
    }
    if (Size != 0) {
        /* 本次接收数据写入接收环形缓冲，缓冲满时丢弃本帧 */
        LP_WriteBlockLoopBuffer_INT(&s_uart[com].r_round, s_uart[com].p_dma_rx, Size);
        s_uart[com].rx_idle = 1;                                       /* DMA+空闲本身就是按帧交付，同步置帧标志 */
    }

    uart_rx_dma_restart(com);                                          /* 立即重启DMA接收 */
}

/*******************************************************************
** 函数名	: HAL_UART_TxCpltCallback
** 函数描述	: 中断发送完成回调，继续发送环形缓冲中的剩余数据。
** 参数		: [in] huart: 串口句柄
** 返回		: 无
********************************************************************/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    INT8U com = DRV_UART_COM_MAX;

    if (huart->Instance == USART1) {
        com = DRV_UART_COM_0;
    } else if (huart->Instance == USART2) {
        com = DRV_UART_COM_1;
    } else if (huart->Instance == USART3) {
        com = DRV_UART_COM_2;
    }
    if (com >= DRV_UART_COM_MAX) {
        return;
    }

    s_uart[com].tx_mode = 0;
    uart_tx_kick(com);
}

/*******************************************************************
** 函数名	: USART1_IRQHandler
** 函数描述	: USART1中断入口，交由HAL库分发。
** 参数		: 无
** 返回		: 无
********************************************************************/
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&Uart1Handle);
}

/*******************************************************************
** 函数名	: USART2_IRQHandler
** 函数描述	: USART2中断入口，中断接收走通用ISR（含发送完成分发）。
** 参数		: 无
** 返回		: 无
********************************************************************/
void USART2_IRQHandler(void)
{
    uart_rx_isr(DRV_UART_COM_1);
}

/*******************************************************************
** 函数名	: USART3_IRQHandler
** 函数描述	: USART3中断入口，中断接收走通用ISR（含发送完成分发）。
** 参数		: 无
** 返回		: 无
********************************************************************/
void USART3_IRQHandler(void)
{
    uart_rx_isr(DRV_UART_COM_2);
}

/*******************************************************************
** 函数名	: DMA2_Stream7_IRQHandler
** 函数描述	: USART1 DMA发送通道中断入口，交由HAL库分发。
** 参数		: 无
** 返回		: 无
********************************************************************/
void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

/*******************************************************************
** 函数名	: DMA2_Stream2_IRQHandler
** 函数描述	: USART1 DMA接收通道中断入口，交由HAL库分发。
** 参数		: 无
** 返回		: 无
********************************************************************/
void DMA2_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}
