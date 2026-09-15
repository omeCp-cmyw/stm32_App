/**
  ******************************************************************************
  * @file    bsp_upgrade_usart.c
  * @brief   升级串口UART3驱动：USART3 PB10/PB11，115200 8-N-1
  *          中断接收入环形缓冲，发送阻塞直发，专用于Ymodem升级通道
  ******************************************************************************
  */

#include "bsp_upgrade_usart.h"

/* 接收环形缓冲 */
static uint8_t s_rx_buf[UPGRADE_USART_RX_BUF_SIZE];
static volatile uint32_t s_rx_head;      /* 写入位置（中断上下文推进） */
static volatile uint32_t s_rx_tail;      /* 读取位置（任务上下文推进） */
static UART_HandleTypeDef s_upgrade_uart;

/*******************************************************************
** 函数名	: UPGRADE_USART_Config
** 函数描述	: UART3 GPIO与工作模式配置，使能RXNE中断接收
** 参数		: 无
** 返回		: 无
********************************************************************/
void UPGRADE_USART_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    s_upgrade_uart.Instance          = UPGRADE_USART;
    s_upgrade_uart.Init.BaudRate     = UPGRADE_USART_BAUDRATE;
    s_upgrade_uart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_upgrade_uart.Init.StopBits     = UART_STOPBITS_1;
    s_upgrade_uart.Init.Parity       = UART_PARITY_NONE;
    s_upgrade_uart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_upgrade_uart.Init.Mode         = UART_MODE_TX_RX;

    UPGRADE_USART_CLK_ENABLE();
    UPGRADE_USART_TX_GPIO_CLK_ENABLE();
    UPGRADE_USART_RX_GPIO_CLK_ENABLE();

    /* USART3 GPIO Configuration
       PB10    ------> USART3_TX
       PB11    ------> USART3_RX
    */
    GPIO_InitStruct.Pin       = UPGRADE_USART_TX_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = UPGRADE_USART_TX_AF;
    HAL_GPIO_Init(UPGRADE_USART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = UPGRADE_USART_RX_PIN;
    GPIO_InitStruct.Alternate = UPGRADE_USART_RX_AF;
    HAL_GPIO_Init(UPGRADE_USART_RX_GPIO_PORT, &GPIO_InitStruct);

    HAL_UART_Init(&s_upgrade_uart);

    s_rx_head = 0;
    s_rx_tail = 0;

    /* 使能接收中断 */
    __HAL_UART_ENABLE_IT(&s_upgrade_uart, UART_IT_RXNE);

    HAL_NVIC_SetPriority(UPGRADE_USART_IRQ, 1, 0);
    HAL_NVIC_EnableIRQ(UPGRADE_USART_IRQ);
}

/*******************************************************************
** 函数名	: UPGRADE_USART_IrqProcess
** 函数描述	: UART3中断处理（由stm32f4xx_it.c的USART3_IRQHandler调用）：
**			RXNE读DR逐字节写入接收环形缓冲，缓冲满时丢弃本字节
** 参数		: 无
** 返回		: 无
********************************************************************/
void UPGRADE_USART_IrqProcess(void)
{
    uint32_t next;

    if (__HAL_UART_GET_FLAG(&s_upgrade_uart, UART_FLAG_RXNE) != RESET) {
        uint8_t byte = (uint8_t)READ_REG(s_upgrade_uart.Instance->DR);

        next = (s_rx_head + 1) % UPGRADE_USART_RX_BUF_SIZE;
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = byte;
            s_rx_head = next;
        }
    }

    /* 其余标志（含发送完成）交HAL分发 */
    HAL_UART_IRQHandler(&s_upgrade_uart);
}

/*******************************************************************
** 函数名	: UPGRADE_USART_ReadChar
** 函数描述	: 从接收环形缓冲读取一个字节
** 参数		: 无
** 返回		: 成功返回数据，缓冲空返回-1
********************************************************************/
int32_t UPGRADE_USART_ReadChar(void)
{
    uint8_t byte;

    if (s_rx_head == s_rx_tail) {
        return -1;
    }

    byte = s_rx_buf[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % UPGRADE_USART_RX_BUF_SIZE;

    return (int32_t)byte;
}

/*******************************************************************
** 函数名	: UPGRADE_USART_GetRecvBytes
** 函数描述	: 获取接收环形缓冲中已收字节数
** 参数		: 无
** 返回		: 已收字节数
********************************************************************/
uint32_t UPGRADE_USART_GetRecvBytes(void)
{
    return (s_rx_head - s_rx_tail + UPGRADE_USART_RX_BUF_SIZE) % UPGRADE_USART_RX_BUF_SIZE;
}

/*******************************************************************
** 函数名	: UPGRADE_USART_WriteByte
** 函数描述	: 阻塞方式直发一个字节（Ymodem出向仅单字节控制字符，
**			阻塞直发保证时序敏感应答ACK/NAK/CAN即时上总线）
** 参数		: [in] data: 字节数据
** 返回		: 1成功，0失败
********************************************************************/
uint8_t UPGRADE_USART_WriteByte(uint8_t data)
{
    if (HAL_UART_Transmit(&s_upgrade_uart, &data, 1, 1000) != HAL_OK) {
        return 0;
    }
    return 1;
}

/*********************************************END OF FILE**********************/
