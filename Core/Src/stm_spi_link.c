#include "stm_spi_link.h"

#define STM_SPI_MAGIC_REQ  0xA5U
#define STM_SPI_MAGIC_RESP 0x5AU
#define STM_SPI_CMD_FREQ   0x01U
#define STM_SPI_FRAME_LEN  8U

static uint32_t s_current_freq_hz = 35000000UL;
static uint8_t s_sequence = 0;

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

static void prepare_response(uint8_t *tx)
{
    uint32_t f = s_current_freq_hz;

    tx[0] = STM_SPI_MAGIC_RESP;
    tx[1] = 0x00;
    tx[2] = (uint8_t)f;
    tx[3] = (uint8_t)(f >> 8);
    tx[4] = (uint8_t)(f >> 16);
    tx[5] = (uint8_t)(f >> 24);
    tx[6] = s_sequence;
    tx[7] = checksum7(tx);
}

static bool parse_request(const uint8_t *rx, uint32_t *out_freq_hz)
{
    if ((rx[0] != STM_SPI_MAGIC_REQ) || (rx[1] != STM_SPI_CMD_FREQ) ||
        (rx[7] != checksum7(rx)))
    {
        return false;
    }

    uint32_t freq = ((uint32_t)rx[2]) |
                    ((uint32_t)rx[3] << 8) |
                    ((uint32_t)rx[4] << 16) |
                    ((uint32_t)rx[5] << 24);
    *out_freq_hz = clamp_freq(freq);
    return true;
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

static bool spi2_transfer_frame(const uint8_t *tx, uint8_t *rx)
{
    spi2_clear_rx();

    if (!wait_nss_low())
    {
        return false;
    }

    for (int i = 0; i < STM_SPI_FRAME_LEN; i++)
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
    uint8_t tx[STM_SPI_FRAME_LEN] = {0};
    uint8_t rx[STM_SPI_FRAME_LEN] = {0};

    prepare_response(tx);

    if (!spi2_transfer_frame(tx, rx))
    {
        return false;
    }

    uint32_t freq_hz = 0;
    if (!parse_request(rx, &freq_hz))
    {
        return false;
    }

    s_sequence++;
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
