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

void STM_ADC_Init(void);
uint16_t STM_ADC_ReadMicRaw(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM_ADC_H__ */
