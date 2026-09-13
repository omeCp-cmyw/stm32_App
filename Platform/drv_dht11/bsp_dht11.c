
#include "bsp_dht11.h"


/* Include header files */
#include "bsp_dht11.h"
#include "../Tools/dwt_delay/core_delay.h"  
#include "FreeRTOS.h"
#include "task.h"
/* Private type definitions */
/* Private macros */
/* Private variables */
/* Extended variables */
/* Private function prototypes */
static void DHT11_Mode_IPU(void);
static void DHT11_Mode_Out_PP(void);
static uint8_t DHT11_ReadByte(void);

/* Function implementations */

/**
  * @brief  DHT11 initialization function
  * @param  None
  * @retval None
  */
void DHT11_Init ( void )
{
  DHT11_Dout_GPIO_CLK_ENABLE();
  
  DHT11_Mode_Out_PP();
        
  DHT11_Dout_HIGH();  // Pull up GPIO
}

/**
  * @brief  Set DHT11-DATA pin to pull-up input mode
  * @param  None
  * @retval None
  */
static void DHT11_Mode_IPU(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;
  
  /* GPIO configuration for input */
  GPIO_InitStruct.Pin   = DHT11_Dout_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_Dout_PORT, &GPIO_InitStruct);
        
}

/**
  * @brief  Set DHT11-DATA pin to push-pull output mode
  * @param  None
  * @retval None
  */
static void DHT11_Mode_Out_PP(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;
  
  /* GPIO configuration for output */
  GPIO_InitStruct.Pin = DHT11_Dout_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_Dout_PORT, &GPIO_InitStruct);          
}


/**
  * @brief  Read one byte from DHT11, MSB first
  * @param  None
  * @retval Data read from DHT11
  */
static uint8_t DHT11_ReadByte ( void )
{
    uint8_t i, temp=0;
    uint16_t count;
    for(i=0;i<8;i++)    
    {         
        /* Each bit starts with 50us low level, wait until master sends 50us low level ends */  
        while(DHT11_Data_IN()==GPIO_PIN_RESET)
        {
          count++;
          if(count>1000)  
            return 0;
          Delay_us(1); 
        }

        /* DHT11 represents "1" with 26~28us high level, "0" with 70us high level,
         * Distinguish these two states by checking the level after x us delay
         */
        Delay_us(50); // Delay x us, this delay needs to be greater than the data duration
        count = 0;
        if(DHT11_Data_IN()==GPIO_PIN_SET)/* After x us, still high level means data "1" */
        {
              /* Wait for data 1 high level to end */
              while(DHT11_Data_IN()==GPIO_PIN_SET)
              {
                count++;
                if(count>1000)  
                  return 0;
                Delay_us(1); 
              }
              count = 0;
              temp|=(uint8_t)(0x01<<(7-i));  // Set bit 7-i to 1, MSB first
        }
        else         // After x us, low level means data "0"
        {                           
           temp&=(uint8_t)~(0x01<<(7-i)); // Set bit 7-i to 0, MSB first
        }
    }
    return temp;
}


/*
 * 
 * 
 */
/**
  * @brief  One complete data transmission is 40bit, MSB first
  * @param  DHT11_Data: DHT11 data type structure
  * @retval ERROR: Read error
  *         SUCCESS: Read success
  * @note   8bit humidity integer + 8bit humidity decimal + 8bit temperature integer + 8bit temperature decimal + 8bit checksum
  */
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *DHT11_Data)
{  
  uint8_t temp;
  uint16_t humi_temp;
  uint16_t count;

  /* Output mode */
  DHT11_Mode_Out_PP();
  /* Master pulls low */
  DHT11_Dout_LOW();
  /* Delay 18ms */
  Delay_ms(18);
  
  /* Bus pull up, master delay 30us */
  DHT11_Dout_HIGH(); 

  Delay_us(30);   // Delay 30us

  /* Master set to input, check slave response signal */ 
  DHT11_Mode_IPU();

//  /* Check if slave has low level response signal, if no response then jump out, if response then continue */
  if(DHT11_Data_IN()==GPIO_PIN_RESET)
  {
    count=0;
    /* Poll until slave sends 80us low level response signal ends */  
    while(DHT11_Data_IN()==GPIO_PIN_RESET)
    {
      count++;
      if(count>100)  
        return 0;
      Delay_us(1); 
    }
    count=0;
    /* Poll until slave sends 80us high level ready signal ends */
    while(DHT11_Data_IN()==GPIO_PIN_SET)
    {
      count++;
      if(count>100)  
        return 0;
      Delay_us(1); 
    }
    
    /* Start receiving data */   
    DHT11_Data->humi_high8bit= DHT11_ReadByte();
    DHT11_Data->humi_low8bit = DHT11_ReadByte();
    DHT11_Data->temp_high8bit= DHT11_ReadByte();
    DHT11_Data->temp_low8bit = DHT11_ReadByte();
    DHT11_Data->check_sum    = DHT11_ReadByte();

    /* Process data */
    humi_temp=DHT11_Data->humi_high8bit*100+DHT11_Data->humi_low8bit;
    DHT11_Data->humidity =(double)humi_temp/100;
    
    humi_temp=DHT11_Data->temp_high8bit*100+DHT11_Data->temp_low8bit;
    DHT11_Data->temperature=(double)humi_temp/100;    
    
    /* Check if the read data is correct */
    temp = DHT11_Data->humi_high8bit + DHT11_Data->humi_low8bit + 
           DHT11_Data->temp_high8bit+ DHT11_Data->temp_low8bit;
    
    if(DHT11_Data->check_sum==temp)
    { 
      return SUCCESS;
    }
    else 
      return ERROR;
  }        
  else
    return ERROR;
}


