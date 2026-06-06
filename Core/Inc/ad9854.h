#ifndef __AD9854_H__
#define __AD9854_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define AD9854_DEFAULT_FREQ_HZ 35000000UL
#define AD9854_FULL_SCALE_AMP  4095U

void AD9854_IO_Init(void);
void AD9854_InitSingle(void);
void AD9854_SetSine(uint32_t freq_hz, uint16_t amplitude);
void AD9854_Stop(void);

#ifdef __cplusplus
}
#endif

#endif
