#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drv_uart1.h"

DMA_HandleTypeDef  hdma_usart1_rx;
DMA_HandleTypeDef  hdma_usart1_tx;
UART_HandleTypeDef Uart1Handle;

static volatile uint8_t    u1_tx_dma_flag;
static volatile uint8_t    u1_rx_dma_flag;

static volatile uint8_t    u1_rx_dma_size;

/* 重定向printf，使用MicroLIB支持串口打印 */
int fputc(int ch, FILE *f)
{
	HAL_UART_Transmit(&Uart1Handle, (uint8_t *)&ch, 1, 1000);		
	return (ch);
}

/*******************************************************************
** 函数名	: UART1_DMA_Config
** 函数描述	: USART1配置，工作模式 115200 8-N-1，包含DMA和中断初始化
** 参数		: 无
** 返回		: 无
********************************************************************/
void UART1_DMA_Config(void)
{ 
    __USART1_CLK_ENABLE();
    
    Uart1Handle.Instance          = USART1;

    Uart1Handle.Init.BaudRate     = UART1_BAUDRATE;
    Uart1Handle.Init.WordLength   = UART_WORDLENGTH_8B;
    Uart1Handle.Init.StopBits     = UART_STOPBITS_1;
    Uart1Handle.Init.Parity       = UART_PARITY_NONE;
    Uart1Handle.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    Uart1Handle.Init.Mode         = UART_MODE_TX_RX;

    HAL_UART_Init(&Uart1Handle);
        
    UART1_Pin_Init();
    
    UART1_DMA_Init();
       
    /* 使用HAL库函数流程可不用单独使能 */
    //__HAL_UART_ENABLE_IT(&Uart1Handle,UART_IT_RXNE);     
    //__HAL_UART_ENABLE_IT(&Uart1Handle,UART_IT_IDLE);  
             
    /* 设置中断优先级，使能中断 */   
    HAL_NVIC_SetPriority(USART1_IRQn ,1,0);	
    HAL_NVIC_EnableIRQ(USART1_IRQn );	
    
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
       
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}


/*******************************************************************
** 函数名	: UART1_Pin_Init
** 函数描述	: USART1 TX/RX 引脚初始化
** 参数		: 无
** 返回		: 无
********************************************************************/
void UART1_Pin_Init()
{  
    GPIO_InitTypeDef  GPIO_InitStruct = {0};
      
    __GPIOA_CLK_ENABLE();

    /* 配置Tx引脚为复用功能  */
    GPIO_InitStruct.Pin = UART1_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = UART1_TX_AF;
    HAL_GPIO_Init(UART1_TX_GPIO_PORT, &GPIO_InitStruct);

    /* 配置Rx引脚为复用功能 */
    GPIO_InitStruct.Pin = UART1_RX_PIN;
    GPIO_InitStruct.Alternate = UART1_RX_AF;
    HAL_GPIO_Init(UART1_RX_GPIO_PORT, &GPIO_InitStruct); 
}


/*******************************************************************
** 函数名	: UART1_DMA_Init
** 函数描述	: USART1 DMA初始化（TX和RX两路）
** 参数		: 无
** 返回		: 无
********************************************************************/
void UART1_DMA_Init()
{  
     __HAL_RCC_DMA2_CLK_ENABLE();
    
    /* USART1 DMA Init */
    hdma_usart1_rx.Instance = DMA2_Stream2;
    hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_usart1_rx);
    
    __HAL_LINKDMA(&Uart1Handle,hdmarx,hdma_usart1_rx);

    /* USART1_TX Init */
    hdma_usart1_tx.Instance = DMA2_Stream7;
    hdma_usart1_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_usart1_tx);
    
    __HAL_LINKDMA(&Uart1Handle,hdmatx,hdma_usart1_tx);

}

/*******************************************************************
** 函数名	: u1_send_string
** 函数描述	: 通过USART1发送字符串
** 参数		: [in] str: 待发送的字符串指针
** 返回		: 无
********************************************************************/
void u1_send_string(char *str)
{
    uint16_t i = strlen(str);    
    HAL_UART_Transmit(&Uart1Handle,(uint8_t *)str,i,1000);  
}

/*******************************************************************
** 函数名	: u1_send_bytes
** 函数描述	: 通过USART1发送字节数组
** 参数		: [in] byte: 待发送的数据指针
**          : [in] len:  数据长度
** 返回		: 无
********************************************************************/
void u1_send_bytes(uint8_t *byte,uint32_t len)
{
    HAL_UART_Transmit(&Uart1Handle,byte,len,1000);
}


/*******************************************************************
** 函数名	: u1_get_tx_dma_flag
** 函数描述	: 获取并清除DMA发送完成标志
** 参数		: 无
** 返回		: 1=发送完成, 0=未完成
********************************************************************/
uint8_t  u1_get_tx_dma_flag(void)
{
    if(u1_tx_dma_flag == 1)
    {
        u1_tx_dma_flag = 0;
        return 1;
    }
    else
    {
        return 0;
    }
}
    
/*******************************************************************
** 函数名	: u1_get_rx_dma_flag
** 函数描述	: 获取并清除DMA接收完成标志
** 参数		: 无
** 返回		: 1=接收完成, 0=未完成
********************************************************************/
uint8_t  u1_get_rx_dma_flag(void)
{
    if(u1_rx_dma_flag == 1)
    {
        u1_rx_dma_flag = 0;
        return 1;
    }
    else
    {
        return 0;
    }       
}    


/*******************************************************************
** 函数名	: u1_get_rx_dma_size
** 函数描述	: 获取并清除本次DMA接收的数据长度
** 参数		: 无
** 返回		: 接收到的数据字节数
********************************************************************/
uint16_t  u1_get_rx_dma_size(void)
{
    if(u1_rx_dma_size != 0)
    {
        uint16_t ret_size = u1_rx_dma_size;
        u1_rx_dma_size = 0;
        return ret_size;
    }
    else
    {
        return 0;
    }      
}




/* DMA发送完成中断回调，置位发送完成标志 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
   if(huart->Instance == USART1)
   {
        u1_tx_dma_flag = 1;             
   }          
}


/* DMA接收完成/空闲中断回调，记录接收数据长度 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if(huart -> Instance == USART1)
	{	
        if(huart->RxEventType == HAL_UART_RXEVENT_IDLE)
        {
             /* 从回调函数参数获取当前接收个数 */
             u1_rx_dma_size = Size;        
        } 
                
        /* 需要特别判断刚好接收满的状态 */
        if( __HAL_DMA_GET_COUNTER(&hdma_usart1_rx) == 0)
        {
             /* 从回调函数参数获取当前接收个数 */
             u1_rx_dma_size = Size;        
        }        
	}
}


void USART1_IRQHandler()
{
    HAL_UART_IRQHandler(&Uart1Handle);        
}

/*********************************************END OF FILE**********************/
