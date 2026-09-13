#ifndef __CORE_DELAY_H
#define __CORE_DELAY_H

#include "core_delay.h"
#include "stdint.h"

#define USE_DWT_DELAY			1	/* Use DWT kernel precision delay */

#if USE_DWT_DELAY
#define USE_TICK_DELAY		0		/* Do not use SysTick delay */
#else
#define USE_TICK_DELAY		1		/* Use SysTick delay */
#endif

#if USE_DWT_DELAY

#define Delay_ms(ms)  	CPU_TS_Tmr_Delay_MS(ms)
#define Delay_us(us)  	CPU_TS_Tmr_Delay_US(us)
/* Max delay 60s=2^32/72000000 */
#define Delay_s(s)  	  CPU_TS_Tmr_Delay_S(s)

/* Get kernel clock frequency */            
#define GET_CPU_ClkFreq()       (168000000)
#define SysClockFreq            (168000000)
/* For convenience, call CPU_TS_TmrInit function inside delay function to initialize timestamp register,
   so each function call will initialize once.
   Set this macro to 0, then call CPU_TS_TmrInit in main function at startup to avoid repeated initialization */  

#define CPU_TS_INIT_IN_DELAY_FUNCTION   0  


/*******************************************************************************
 * 							Function declarations
 ******************************************************************************/
uint32_t CPU_TS_TmrRd(void);
void CPU_TS_TmrInit(void);

/* Before using the following functions, you must first call CPU_TS_TmrInit function to enable counter,
   or enable macro CPU_TS_INIT_IN_DELAY_FUNCTION */
/* Maximum delay value is 60 seconds */
void CPU_TS_Tmr_Delay_US(uint32_t us);
#define CPU_TS_Tmr_Delay_MS(ms)     CPU_TS_Tmr_Delay_US(ms*1000)
#define CPU_TS_Tmr_Delay_S(s)       CPU_TS_Tmr_Delay_MS(s*1000)

#endif

#endif /* __CORE_DELAY_H */
