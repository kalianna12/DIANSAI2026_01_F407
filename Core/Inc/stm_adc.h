#ifndef __STM_ADC_H__
#define __STM_ADC_H__

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STM_MIC_ADC_GPIO_PORT GPIOB
#define STM_MIC_ADC_GPIO_PIN  GPIO_PIN_0
#define STM_MIC_ADC_CHANNEL   8U
#define STM_ADC_RATE_16K_HZ   16000UL
#define STM_ADC_RATE_44K_HZ   44100UL
#define STM_ADC_DMA_BUF_LEN   2048U

void STM_ADC_Init(void);
void STM_ADC_SetSampleRate(uint32_t sample_rate_hz);
uint32_t STM_ADC_GetSampleRate(void);
uint16_t STM_ADC_ReadMicRaw(void);
uint16_t STM_ADC_CopyLatest(uint16_t *out, uint16_t max_points);

#ifdef __cplusplus
}
#endif

#endif /* __STM_ADC_H__ */
