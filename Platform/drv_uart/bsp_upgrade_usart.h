#ifndef __BSP_UPGRADE_USART_H
#define	__BSP_UPGRADE_USART_H

#include "stm32f4xx.h"
#include <stdint.h>

/*
 * 升级串口UART3：USART3 PB10(TX)/PB11(RX)，115200 8-N-1
 * 专用于Ymodem本地固件升级通道，与日志串口USART1物理分离，
 * 中断接收+环形缓冲，发送为阻塞直发（Ymodem出向仅单字节控制字符）
 */

#define UPGRADE_USART_BAUDRATE                  115200

/* Pin definitions */
#define UPGRADE_USART                           USART3
#define UPGRADE_USART_CLK_ENABLE()              __USART3_CLK_ENABLE()

#define UPGRADE_USART_TX_GPIO_PORT              GPIOB
#define UPGRADE_USART_TX_GPIO_CLK_ENABLE()      __GPIOB_CLK_ENABLE()
#define UPGRADE_USART_TX_PIN                    GPIO_PIN_10
#define UPGRADE_USART_TX_AF                     GPIO_AF7_USART3

#define UPGRADE_USART_RX_GPIO_PORT              GPIOB
#define UPGRADE_USART_RX_GPIO_CLK_ENABLE()      __GPIOB_CLK_ENABLE()
#define UPGRADE_USART_RX_PIN                    GPIO_PIN_11
#define UPGRADE_USART_RX_AF                     GPIO_AF7_USART3

#define UPGRADE_USART_IRQ                       USART3_IRQn
#define UPGRADE_USART_IRQHandler                USART3_IRQHandler

/* 接收环形缓冲容量：容纳Ymodem 1K包的1029字节并留余量 */
#define UPGRADE_USART_RX_BUF_SIZE               1280

void UPGRADE_USART_Config(void);
void UPGRADE_USART_IrqProcess(void);
int32_t UPGRADE_USART_ReadChar(void);
uint32_t UPGRADE_USART_GetRecvBytes(void);
uint8_t UPGRADE_USART_WriteByte(uint8_t data);

#endif /* __BSP_UPGRADE_USART_H */
