#include "stm_adc.h"
#include <stdbool.h>

static volatile uint16_t s_adc_dma_buf[STM_ADC_DMA_BUF_LEN];
static uint32_t s_sample_rate_hz = STM_ADC_RATE_16K_HZ;
static bool s_adc_enabled = false;

static uint32_t clamp_rate(uint32_t sample_rate_hz)
{
    return (sample_rate_hz >= 30000UL) ? STM_ADC_RATE_44K_HZ : STM_ADC_RATE_16K_HZ;
}

static void tim3_set_rate(uint32_t sample_rate_hz)
{
    const uint32_t tim_clk_hz = 84000000UL;
    uint32_t ticks = (tim_clk_hz + (sample_rate_hz / 2UL)) / sample_rate_hz;
    if (ticks < 2UL)
    {
        ticks = 2UL;
    }
    if (ticks > 65536UL)
    {
        ticks = 65536UL;
    }

    TIM3->CR1 = 0;
    TIM3->PSC = 0;
    TIM3->ARR = (uint16_t)(ticks - 1UL);
    TIM3->CNT = 0;
    TIM3->CR2 = TIM_CR2_MMS_1;  // Update event as TRGO.
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_CEN;
}

void STM_ADC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    GPIO_InitStruct.Pin = STM_MIC_ADC_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(STM_MIC_ADC_GPIO_PORT, &GPIO_InitStruct);

    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U)
    {
    }
    DMA2->LIFCR = DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CTEIF0 |
                  DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)s_adc_dma_buf;
    DMA2_Stream0->NDTR = STM_ADC_DMA_BUF_LEN;
    DMA2_Stream0->FCR = 0;
    DMA2_Stream0->CR = DMA_SxCR_PL_1 | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 |
                       DMA_SxCR_MINC | DMA_SxCR_CIRC;

    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;  // PCLK2 / 4 = 21 MHz when PCLK2 is 84 MHz.

    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SQR1 = 0;                // One conversion.
    ADC1->SQR2 = 0;
    ADC1->SQR3 = STM_MIC_ADC_CHANNEL;
    ADC1->SMPR2 &= ~(7U << (STM_MIC_ADC_CHANNEL * 3U));
    ADC1->SMPR2 |=  (6U << (STM_MIC_ADC_CHANNEL * 3U));  // 144 cycles sample time.
    ADC1->CR2 = ADC_CR2_EXTSEL_3 | ADC_CR2_EXTEN_0 | ADC_CR2_DMA |
                ADC_CR2_DDS | ADC_CR2_ADON;

    for (volatile uint32_t i = 0; i < 10000U; i++)
    {
    }

    DMA2_Stream0->CR |= DMA_SxCR_EN;
    s_adc_enabled = true;
    STM_ADC_SetSampleRate(STM_ADC_RATE_16K_HZ);
}

void STM_ADC_SetSampleRate(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = clamp_rate(sample_rate_hz);
    if (!s_adc_enabled)
    {
        return;
    }
    tim3_set_rate(s_sample_rate_hz);
}

uint32_t STM_ADC_GetSampleRate(void)
{
    return s_sample_rate_hz;
}

uint16_t STM_ADC_ReadMicRaw(void)
{
    if (!s_adc_enabled)
    {
        return 0;
    }

    uint32_t write = STM_ADC_DMA_BUF_LEN - DMA2_Stream0->NDTR;
    if (write >= STM_ADC_DMA_BUF_LEN)
    {
        write = 0;
    }

    uint32_t index = (write == 0U) ? (STM_ADC_DMA_BUF_LEN - 1U) : (write - 1U);
    return s_adc_dma_buf[index] & 0x0FFFU;
}

uint16_t STM_ADC_CopyLatest(uint16_t *out, uint16_t max_points)
{
    if ((out == 0) || (max_points == 0U))
    {
        return 0;
    }
    if (!s_adc_enabled)
    {
        return 0;
    }

    if (max_points > STM_ADC_DMA_BUF_LEN)
    {
        max_points = STM_ADC_DMA_BUF_LEN;
    }

    uint32_t write = STM_ADC_DMA_BUF_LEN - DMA2_Stream0->NDTR;
    if (write >= STM_ADC_DMA_BUF_LEN)
    {
        write = 0;
    }

    uint32_t start = (write + STM_ADC_DMA_BUF_LEN - max_points) % STM_ADC_DMA_BUF_LEN;
    for (uint16_t i = 0; i < max_points; i++)
    {
        out[i] = s_adc_dma_buf[(start + i) % STM_ADC_DMA_BUF_LEN] & 0x0FFFU;
    }
    return max_points;
}
