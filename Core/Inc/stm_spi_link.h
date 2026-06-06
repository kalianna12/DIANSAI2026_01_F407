#ifndef __STM_SPI_LINK_H__
#define __STM_SPI_LINK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define STM_SPI_FREQ_MIN_HZ 30000000UL
#define STM_SPI_FREQ_MAX_HZ 40000000UL

void STM_SPI_Link_Init(uint32_t initial_freq_hz);
bool STM_SPI_Link_Poll(uint32_t *out_freq_hz);
void STM_SPI_Link_SetCurrentFreq(uint32_t freq_hz);

#ifdef __cplusplus
}
#endif

#endif
