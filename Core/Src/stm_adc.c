#include "stm_adc.h"

void STM_ADC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitStruct.Pin = STM_MIC_ADC_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(STM_MIC_ADC_GPIO_PORT, &GPIO_InitStruct);

    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;  // PCLK2 / 4 = 21 MHz when PCLK2 is 84 MHz.

    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SQR1 = 0;                // One conversion.
    ADC1->SQR2 = 0;
    ADC1->SQR3 = STM_MIC_ADC_CHANNEL;
    ADC1->SMPR2 &= ~(7U << (STM_MIC_ADC_CHANNEL * 3U));
    ADC1->SMPR2 |=  (6U << (STM_MIC_ADC_CHANNEL * 3U));  // 144 cycles sample time.
    ADC1->CR2 = ADC_CR2_ADON;

    for (volatile uint32_t i = 0; i < 10000U; i++)
    {
    }
    (void)STM_ADC_ReadMicRaw();
}

uint16_t STM_ADC_ReadMicRaw(void)
{
    ADC1->SR = 0;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    uint32_t guard = 100000U;
    while (((ADC1->SR & ADC_SR_EOC) == 0U) && (--guard > 0U))
    {
    }
    if (guard == 0U)
    {
        return 0;
    }

    return (uint16_t)(ADC1->DR & 0x0FFFU);
}
