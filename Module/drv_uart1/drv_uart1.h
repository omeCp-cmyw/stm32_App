#ifndef __DRV_UART1_H
#define	__DRV_UART1_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32f4xx.h"

extern  DMA_HandleTypeDef  hdma_usart1_rx;
extern  DMA_HandleTypeDef  hdma_usart1_tx;
extern  UART_HandleTypeDef Uart1Handle;

/************************************************************/
#define UART1_BAUDRATE                    115200

#define UART1_RX_GPIO_PORT                GPIOA
#define UART1_RX_PIN                      GPIO_PIN_10
#define UART1_RX_AF                       GPIO_AF7_USART1

#define UART1_TX_GPIO_PORT                GPIOA
#define UART1_TX_PIN                      GPIO_PIN_9
#define UART1_TX_AF                       GPIO_AF7_USART1

/************************************************************/

void UART1_DMA_Config(void);
void UART1_Pin_Init(void);
void UART1_DMA_Init(void);

void     u1_send_string(char *str);
void     u1_send_bytes(uint8_t *byte,uint32_t len);

uint8_t  u1_get_tx_dma_flag(void);
uint8_t  u1_get_rx_dma_flag(void);

uint16_t u1_get_rx_dma_size(void);

int fputc(int ch, FILE *f);


#endif /* __DRV_UART1_H */
