#ifndef __AD9102_H__
#define __AD9102_H__

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AD9102_DEFAULT_FREQ_HZ 1000UL

typedef enum {
    AD9102_WAVE_SINE = 0,
    AD9102_WAVE_SQUARE = 1,
    AD9102_WAVE_TRIANGLE = 2,
    AD9102_WAVE_ARBITRARY = 3,
} ad9102_wave_t;

void AD9102_IO_Init(void);
bool AD9102_Init(void);
bool AD9102_Configure(ad9102_wave_t wave, uint32_t freq_hz);
bool AD9102_SetMode(ad9102_wave_t wave);
ad9102_wave_t AD9102_GetMode(void);
uint32_t AD9102_GetFreqHz(void);

#ifdef __cplusplus
}
#endif

#endif /* __AD9102_H__ */
