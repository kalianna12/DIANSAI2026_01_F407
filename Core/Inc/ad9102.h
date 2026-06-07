#ifndef __AD9102_H__
#define __AD9102_H__

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AD9102_DEFAULT_FREQ_HZ 1000UL
#define AD9102_DEFAULT_AMP     0x0400U

typedef enum {
    AD9102_WAVE_SINE = 0,
    AD9102_WAVE_SQUARE = 1,
    AD9102_WAVE_TRIANGLE = 2,
    AD9102_WAVE_ARBITRARY = 3,
} ad9102_wave_t;

void AD9102_IO_Init(void);
bool AD9102_Init(void);
bool AD9102_Configure(ad9102_wave_t wave, uint32_t freq_hz, uint16_t amplitude);
bool AD9102_SetMode(ad9102_wave_t wave);
bool AD9102_SetFrequency(uint32_t freq_hz);
bool AD9102_SetAmplitude(uint16_t amplitude);
ad9102_wave_t AD9102_GetMode(void);
uint32_t AD9102_GetFreqHz(void);
uint16_t AD9102_GetAmplitude(void);

#ifdef __cplusplus
}
#endif

#endif /* __AD9102_H__ */
