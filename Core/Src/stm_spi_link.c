#include "stm_spi_link.h"
#include "stm_adc.h"
#include "ad9102.h"

#define STM_SPI_MAGIC_REQ  0xA5U
#define STM_SPI_MAGIC_RESP 0x5AU
#define STM_SPI_CMD_FREQ   0x01U
#define STM_SPI_CMD_ADC    0x02U
#define STM_SPI_CMD_RATE   0x03U
#define STM_SPI_CMD_WAVE   0x04U
#define STM_SPI_CMD_AD9102 0x05U
#define STM_SPI_CMD_AD9102_AMP 0x06U
#define STM_SPI_FRAME_LEN  8U
#define STM_SPI_WAVE_MAX_POINTS 512U
#define STM_SPI_WAVE_FRAME_LEN  (8U + (STM_SPI_WAVE_MAX_POINTS * 2U))

static uint32_t s_current_freq_hz = 35000000UL;
static uint8_t s_sequence = 0;
static uint8_t s_next_response_cmd = STM_SPI_CMD_FREQ;
static uint8_t s_last_ad9102_mode = STM_SPI_AD9102_SINE;
static uint16_t s_wave_points = 256U;
static uint8_t s_tx_frame[STM_SPI_WAVE_FRAME_LEN];
static uint8_t s_rx_frame[STM_SPI_WAVE_FRAME_LEN];
static uint16_t s_wave_temp[STM_SPI_WAVE_MAX_POINTS];

static uint8_t checksum7(const uint8_t *data)
{
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++)
    {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static uint32_t clamp_freq(uint32_t freq_hz)
{
    if (freq_hz < STM_SPI_FREQ_MIN_HZ)
    {
        return STM_SPI_FREQ_MIN_HZ;
    }
    if (freq_hz > STM_SPI_FREQ_MAX_HZ)
    {
        return STM_SPI_FREQ_MAX_HZ;
    }
    return freq_hz;
}

static uint8_t checksum_bytes(const uint8_t *data, uint16_t len, int skip_index)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        if ((int)i != skip_index)
        {
            sum = (uint8_t)(sum + data[i]);
        }
    }
    return sum;
}

static uint16_t prepare_response(uint8_t *tx)
{
    for (uint16_t i = 0; i < STM_SPI_WAVE_FRAME_LEN; i++)
    {
        tx[i] = 0;
    }

    tx[0] = STM_SPI_MAGIC_RESP;
    tx[1] = s_next_response_cmd;

    if (s_next_response_cmd == STM_SPI_CMD_ADC)
    {
        uint16_t adc = STM_ADC_ReadMicRaw();
        tx[2] = (uint8_t)adc;
        tx[3] = (uint8_t)(adc >> 8);
        tx[4] = 0;
        tx[5] = 0;
    }
    else if (s_next_response_cmd == STM_SPI_CMD_RATE)
    {
        uint32_t rate = STM_ADC_GetSampleRate();
        tx[2] = (uint8_t)rate;
        tx[3] = (uint8_t)(rate >> 8);
        tx[4] = (uint8_t)(rate >> 16);
        tx[5] = (uint8_t)(rate >> 24);
    }
    else if (s_next_response_cmd == STM_SPI_CMD_WAVE)
    {
        uint16_t copied = STM_ADC_CopyLatest(s_wave_temp, s_wave_points);
        uint32_t rate = STM_ADC_GetSampleRate();

        tx[2] = (uint8_t)copied;
        tx[3] = (uint8_t)(copied >> 8);
        tx[4] = (rate == STM_ADC_RATE_44K_HZ) ? 1U : 0U;
        tx[5] = 0;
        tx[6] = s_sequence;
        tx[7] = 0;

        for (uint16_t i = 0; i < copied; i++)
        {
            tx[8U + (i * 2U)] = (uint8_t)s_wave_temp[i];
            tx[9U + (i * 2U)] = (uint8_t)(s_wave_temp[i] >> 8);
        }
        uint16_t len = (uint16_t)(8U + (copied * 2U));
        tx[7] = checksum_bytes(tx, len, 7);
        return len;
    }
    else if (s_next_response_cmd == STM_SPI_CMD_AD9102)
    {
        uint32_t f = AD9102_GetFreqHz();
        tx[2] = s_last_ad9102_mode;
        tx[3] = (uint8_t)f;
        tx[4] = (uint8_t)(f >> 8);
        tx[5] = (uint8_t)(f >> 16);
    }
    else if (s_next_response_cmd == STM_SPI_CMD_AD9102_AMP)
    {
        uint16_t amp = AD9102_GetAmplitude();
        tx[2] = (uint8_t)amp;
        tx[3] = (uint8_t)(amp >> 8);
        tx[4] = s_last_ad9102_mode;
        tx[5] = 0;
    }
    else
    {
        uint32_t f = s_current_freq_hz;
        tx[2] = (uint8_t)f;
        tx[3] = (uint8_t)(f >> 8);
        tx[4] = (uint8_t)(f >> 16);
        tx[5] = (uint8_t)(f >> 24);
    }

    tx[6] = s_sequence;
    tx[7] = checksum7(tx);
    return STM_SPI_FRAME_LEN;
}

static uint8_t parse_request(const uint8_t *rx, uint32_t *out_freq_hz, uint32_t *out_rate_hz,
                             uint16_t *out_wave_points)
{
    if ((rx[0] != STM_SPI_MAGIC_REQ) || (rx[7] != checksum7(rx)))
    {
        return 0;
    }

    if (rx[1] == STM_SPI_CMD_ADC)
    {
        return STM_SPI_CMD_ADC;
    }

    if (rx[1] == STM_SPI_CMD_RATE)
    {
        *out_rate_hz = ((uint32_t)rx[2]) |
                       ((uint32_t)rx[3] << 8) |
                       ((uint32_t)rx[4] << 16) |
                       ((uint32_t)rx[5] << 24);
        return STM_SPI_CMD_RATE;
    }

    if (rx[1] == STM_SPI_CMD_WAVE)
    {
        uint16_t points = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
        if (points == 0U)
        {
            points = 256U;
        }
        if (points > STM_SPI_WAVE_MAX_POINTS)
        {
            points = STM_SPI_WAVE_MAX_POINTS;
        }
        *out_wave_points = points;
        return STM_SPI_CMD_WAVE;
    }

    if (rx[1] == STM_SPI_CMD_AD9102)
    {
        uint8_t mode = rx[2];
        if (mode > STM_SPI_AD9102_ARBITRARY)
        {
            return 0;
        }
        *out_freq_hz = (uint32_t)rx[3] |
                       ((uint32_t)rx[4] << 8) |
                       ((uint32_t)rx[5] << 16);
        *out_rate_hz = mode;
        return STM_SPI_CMD_AD9102;
    }

    if (rx[1] == STM_SPI_CMD_AD9102_AMP)
    {
        *out_rate_hz = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
        return STM_SPI_CMD_AD9102_AMP;
    }

    if (rx[1] != STM_SPI_CMD_FREQ)
    {
        return 0;
    }

    uint32_t freq = ((uint32_t)rx[2]) |
                    ((uint32_t)rx[3] << 8) |
                    ((uint32_t)rx[4] << 16) |
                    ((uint32_t)rx[5] << 24);
    *out_freq_hz = clamp_freq(freq);
    return STM_SPI_CMD_FREQ;
}

static void spi2_clear_rx(void)
{
    while ((SPI2->SR & SPI_SR_RXNE) != 0U)
    {
        (void)*(__IO uint8_t *)&SPI2->DR;
    }
    (void)SPI2->SR;
}

static bool wait_nss_low(void)
{
    for (volatile uint32_t i = 0; i < 120000U; i++)
    {
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET)
        {
            return true;
        }
    }
    return false;
}

static bool spi2_transfer_frame(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    spi2_clear_rx();

    if (!wait_nss_low())
    {
        return false;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        uint32_t guard = 250000U;
        while (((SPI2->SR & SPI_SR_TXE) == 0U) && (--guard > 0U))
        {
        }
        if (guard == 0U)
        {
            return false;
        }

        *(__IO uint8_t *)&SPI2->DR = tx[i];

        guard = 250000U;
        while (((SPI2->SR & SPI_SR_RXNE) == 0U) && (--guard > 0U))
        {
        }
        if (guard == 0U)
        {
            return false;
        }

        rx[i] = *(__IO uint8_t *)&SPI2->DR;
    }

    return true;
}

void STM_SPI_Link_Init(uint32_t initial_freq_hz)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    s_current_freq_hz = clamp_freq(initial_freq_hz);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    SPI2->CR1 = 0;
    SPI2->CR2 = 0;
    SPI2->CRCPR = 7;
    SPI2->CR1 = SPI_CR1_SPE;
    spi2_clear_rx();
}

bool STM_SPI_Link_Poll(uint32_t *out_freq_hz)
{
    uint16_t len = prepare_response(s_tx_frame);

    if (!spi2_transfer_frame(s_tx_frame, s_rx_frame, len))
    {
        return false;
    }

    if (s_next_response_cmd == STM_SPI_CMD_WAVE)
    {
        s_next_response_cmd = STM_SPI_CMD_FREQ;
        return false;
    }

    uint32_t freq_hz = 0;
    uint32_t rate_hz = 0;
    uint16_t wave_points = 0;
    uint8_t cmd = parse_request(s_rx_frame, &freq_hz, &rate_hz, &wave_points);
    if (cmd == 0)
    {
        return false;
    }

    s_sequence++;
    s_next_response_cmd = cmd;
    if (cmd == STM_SPI_CMD_ADC)
    {
        return false;
    }
    if (cmd == STM_SPI_CMD_RATE)
    {
        STM_ADC_SetSampleRate(rate_hz);
        return false;
    }
    if (cmd == STM_SPI_CMD_WAVE)
    {
        s_wave_points = wave_points;
        return false;
    }
    if (cmd == STM_SPI_CMD_AD9102)
    {
        s_last_ad9102_mode = (uint8_t)rate_hz;
        (void)AD9102_Configure((ad9102_wave_t)s_last_ad9102_mode, freq_hz, AD9102_GetAmplitude());
        return false;
    }
    if (cmd == STM_SPI_CMD_AD9102_AMP)
    {
        (void)AD9102_SetAmplitude((uint16_t)rate_hz);
        return false;
    }

    s_current_freq_hz = freq_hz;
    if (out_freq_hz != NULL)
    {
        *out_freq_hz = freq_hz;
    }
    return true;
}

void STM_SPI_Link_SetCurrentFreq(uint32_t freq_hz)
{
    s_current_freq_hz = clamp_freq(freq_hz);
}
