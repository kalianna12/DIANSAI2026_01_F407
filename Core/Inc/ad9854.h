#ifndef __AD9854_H__
#define __AD9854_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define AD9854_DEFAULT_FREQ_HZ 35000000UL
#define AD9854_FULL_SCALE_AMP  4095U

/* Control pins — all on GPIOA */
#define AD9854_RST_PIN         GPIO_PIN_0
#define AD9854_UCLK_PIN        GPIO_PIN_1
#define AD9854_WR_PIN          GPIO_PIN_2
#define AD9854_RD_PIN          GPIO_PIN_3
#define AD9854_OSK_PIN         GPIO_PIN_4
#define AD9854_FSK_PIN         GPIO_PIN_5

void AD9854_IO_Init(void);
void AD9854_InitSingle(void);
void AD9854_SetSine(uint32_t freq_hz, uint16_t amplitude);
void AD9854_Stop(void);

#ifdef __cplusplus
}
#endif

#endif
