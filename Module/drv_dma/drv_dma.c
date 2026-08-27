#include "drv_uart1.h"
#include "drv_dma.h"


DMA_HandleTypeDef hdma_memtomem_dma2_stream0;


/*******************************************************************
** 函数名	: MTM_DMA_Init
** 函数描述	: 内存到内存 DMA 初始化（DMA2_Stream0）
** 参数		: 无
** 返回		: 无
********************************************************************/
void MTM_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();  
    
  /* 配置 DMA2_Stream0 内存到内存传输 */
  hdma_memtomem_dma2_stream0.Instance = DMA2_Stream0;
  hdma_memtomem_dma2_stream0.Init.Channel = DMA_CHANNEL_0;
  hdma_memtomem_dma2_stream0.Init.Direction = DMA_MEMORY_TO_MEMORY;
  hdma_memtomem_dma2_stream0.Init.PeriphInc = DMA_PINC_ENABLE;
  hdma_memtomem_dma2_stream0.Init.MemInc = DMA_MINC_ENABLE;
  hdma_memtomem_dma2_stream0.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_memtomem_dma2_stream0.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_memtomem_dma2_stream0.Init.Mode = DMA_NORMAL;
  hdma_memtomem_dma2_stream0.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_memtomem_dma2_stream0.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_memtomem_dma2_stream0.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_memtomem_dma2_stream0.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_memtomem_dma2_stream0.Init.PeriphBurst = DMA_PBURST_SINGLE;
  HAL_DMA_Init(&hdma_memtomem_dma2_stream0);       
}




void DMA2_Stream7_IRQHandler()
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}


void DMA2_Stream2_IRQHandler()
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}


/*********************************************END OF FILE**********************/
