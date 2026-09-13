#ifndef __ONEWIRE_DHT11_H__
#define __ONEWIRE_DHT11_H__

/* Include header files */
#include "stm32f4xx_hal.h"

/* Type definitions */
/************************ DHT11 data type definition ******************************/
typedef struct
{
  uint8_t  humi_high8bit;                //Raw data: humidity high 8 bits
  uint8_t  humi_low8bit;                 //Raw data: humidity low 8 bits
  uint8_t  temp_high8bit;                //Raw data: temperature high 8 bits
  uint8_t  temp_low8bit;                 //Raw data: temperature low 8 bits
  uint8_t  check_sum;                    //Checksum
  double    humidity;        //Actual humidity
  double    temperature;     //Actual temperature  
} DHT11_Data_TypeDef;

/* Macros */
/***********************   DHT11 connection pin definition  **************************/
#define DHT11_Dout_GPIO_CLK_ENABLE()              __HAL_RCC_GPIOE_CLK_ENABLE()
#define DHT11_Dout_PORT                           GPIOE
#define DHT11_Dout_PIN                            GPIO_PIN_3

/***********************   DHT11 function macros  ****************************/
#define DHT11_Dout_LOW()                          HAL_GPIO_WritePin(DHT11_Dout_PORT,DHT11_Dout_PIN,GPIO_PIN_RESET) 
#define DHT11_Dout_HIGH()                         HAL_GPIO_WritePin(DHT11_Dout_PORT,DHT11_Dout_PIN,GPIO_PIN_SET)
#define DHT11_Data_IN()                           HAL_GPIO_ReadPin(DHT11_Dout_PORT,DHT11_Dout_PIN)

/* Extended variables */
/* Function declarations */
void DHT11_Init( void );
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef * DHT11_Data);

#endif /* __ONEWIRE_DHT11_H__ */

