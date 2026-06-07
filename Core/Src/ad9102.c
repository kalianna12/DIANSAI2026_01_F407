#include "ad9102.h"

/*
 * AD9102 software-SPI wiring, chosen to avoid AD9854 PA0-PA5/PC0-PC13,
 * STM<->ESP SPI2 PB12-PB15 and MIC ADC PB0.
 *
 *   AD9102 SCLK    -> PD0
 *   AD9102 RESET_N -> PD1
 *   AD9102 SDIO    -> PD2  (STM MOSI)
 *   AD9102 TRIGGER -> PD3
 *   AD9102 SDO     -> PD4  (STM MISO, optional but useful)
 *   AD9102 CS_N    -> PD5
 */
#define AD9102_GPIO_PORT      GPIOD
#define AD9102_PIN_SCLK       GPIO_PIN_0
#define AD9102_PIN_RESET_N    GPIO_PIN_1
#define AD9102_PIN_MOSI       GPIO_PIN_2
#define AD9102_PIN_TRIGGER    GPIO_PIN_3
#define AD9102_PIN_MISO       GPIO_PIN_4
#define AD9102_PIN_CS_N       GPIO_PIN_5

#define AD9102_DAC_CLK_HZ     100000000UL
#define AD9102_DEFAULT_GAIN   0x0400U

#define REG_SPICONFIG       0x0000
#define REG_DACDOF          0x0025
#define REG_RAMUPDATE       0x001D
#define REG_PAT_STATUS      0x001E
#define REG_PAT_TYPE        0x001F
#define REG_WAV_CONFIG      0x0027
#define REG_PAT_TIMEBASE    0x0028
#define REG_PAT_PERIOD      0x0029
#define REG_DAC_DGAIN       0x0035
#define REG_DDS_TW32        0x003E
#define REG_DDS_TW1         0x003F
#define REG_DDS_PW          0x0043
#define REG_DDS_CONFIG      0x0045
#define REG_TW_RAM_CONFIG   0x0047
#define REG_START_DLY       0x005C
#define REG_START_ADDR      0x005D
#define REG_STOP_ADDR       0x005E
#define REG_SRAM_DATA       0x6000

#define WAV_CONFIG_PRESTORE_DDS   (3U << 4)
#define WAV_CONFIG_WAVE_PRESTORE  1U
#define WAV_CONFIG_DDS_CONTINUOUS (WAV_CONFIG_PRESTORE_DDS | WAV_CONFIG_WAVE_PRESTORE)
#define WAV_CONFIG_WAVE_RAM       0U
#define DAC_12BIT_FIELD(v)        ((uint16_t)(((uint16_t)(v) & 0x0FFFU) << 4))
#define ADDR_12BIT_FIELD(v)       ((uint16_t)(((uint16_t)(v) & 0x0FFFU) << 5))

static ad9102_wave_t s_wave = AD9102_WAVE_SINE;
static uint32_t s_freq_hz = AD9102_DEFAULT_FREQ_HZ;

static const uint16_t s_square[2] = {
    0x000, 0x0FFF,
};

static const uint16_t s_triangle[32] = {
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
    0xFFF, 0xEEE, 0xDDD, 0xCCC, 0xBBB, 0xAAA, 0x999, 0x888,
    0x777, 0x666, 0x555, 0x444, 0x333, 0x222, 0x111, 0x000,
};

static const uint16_t s_stair[16] = {
    0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700,
    0x800, 0x900, 0xA00, 0xB00, 0xC00, 0xD00, 0xE00, 0xF00,
};

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- > 0U)
    {
        __NOP();
    }
}

static inline void pin_set(uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(AD9102_GPIO_PORT, pin, state);
}

static uint8_t spi_transfer_byte(uint8_t tx)
{
    uint8_t rx = 0;

    for (int i = 7; i >= 0; i--)
    {
        pin_set(AD9102_PIN_SCLK, GPIO_PIN_RESET);
        pin_set(AD9102_PIN_MOSI, (tx & (1U << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        delay_cycles(12U);

        pin_set(AD9102_PIN_SCLK, GPIO_PIN_SET);
        delay_cycles(12U);
        if (HAL_GPIO_ReadPin(AD9102_GPIO_PORT, AD9102_PIN_MISO) == GPIO_PIN_SET)
        {
            rx |= (uint8_t)(1U << i);
        }
    }

    pin_set(AD9102_PIN_SCLK, GPIO_PIN_RESET);
    return rx;
}

static bool write_reg(uint16_t addr, uint16_t data)
{
    uint16_t cmd = addr & 0x7FFFU;

    pin_set(AD9102_PIN_CS_N, GPIO_PIN_RESET);
    delay_cycles(20U);
    (void)spi_transfer_byte((uint8_t)(cmd >> 8));
    (void)spi_transfer_byte((uint8_t)cmd);
    (void)spi_transfer_byte((uint8_t)(data >> 8));
    (void)spi_transfer_byte((uint8_t)data);
    delay_cycles(20U);
    pin_set(AD9102_PIN_CS_N, GPIO_PIN_SET);
    delay_cycles(80U);
    return true;
}

static bool read_reg(uint16_t addr, uint16_t *data)
{
    uint16_t cmd = 0x8000U | (addr & 0x7FFFU);
    uint8_t hi;
    uint8_t lo;

    if (data == NULL)
    {
        return false;
    }

    pin_set(AD9102_PIN_CS_N, GPIO_PIN_RESET);
    delay_cycles(20U);
    (void)spi_transfer_byte((uint8_t)(cmd >> 8));
    (void)spi_transfer_byte((uint8_t)cmd);
    hi = spi_transfer_byte(0x00);
    lo = spi_transfer_byte(0x00);
    delay_cycles(20U);
    pin_set(AD9102_PIN_CS_N, GPIO_PIN_SET);
    delay_cycles(80U);

    *data = ((uint16_t)hi << 8) | lo;
    return true;
}

static bool ram_update(void)
{
    return write_reg(REG_RAMUPDATE, 0x0001);
}

static bool write_dds_frequency(uint32_t freq_hz)
{
    if (freq_hz == 0U)
    {
        freq_hz = 1U;
    }
    if (freq_hz >= AD9102_DAC_CLK_HZ)
    {
        freq_hz = AD9102_DAC_CLK_HZ - 1U;
    }

    uint64_t tw64 = ((uint64_t)freq_hz << 24) / AD9102_DAC_CLK_HZ;
    uint32_t tw = (uint32_t)(tw64 & 0x00FFFFFFUL);
    uint16_t tw_msb = (uint16_t)((tw >> 8) & 0xFFFFU);
    uint16_t tw_lsb = (uint16_t)((tw & 0xFFU) << 8);

    return write_reg(REG_DDS_TW32, tw_msb) &&
           write_reg(REG_DDS_TW1, tw_lsb) &&
           write_reg(REG_DDS_PW, 0x0000);
}

static bool write_sram_waveform(const uint16_t *samples, uint16_t count)
{
    if (samples == NULL || count == 0U)
    {
        return false;
    }

    if (!write_reg(REG_PAT_STATUS, 0x0004))
    {
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
    {
        if (!write_reg((uint16_t)(REG_SRAM_DATA + i), (uint16_t)(samples[i] & 0x0FFFU)))
        {
            return false;
        }
    }
    return write_reg(REG_PAT_STATUS, 0x0000);
}

static bool configure_sram_waveform(const uint16_t *samples, uint16_t count, uint32_t freq_hz)
{
    uint32_t pattern_cycles;
    uint32_t hold_cycles = 1U;
    uint64_t denom;

    if (count < 2U)
    {
        return false;
    }

    denom = (uint64_t)freq_hz * count;
    if (denom != 0U)
    {
        hold_cycles = (uint32_t)((AD9102_DAC_CLK_HZ + (denom / 2U)) / denom);
    }
    if (hold_cycles < 1U)
    {
        hold_cycles = 1U;
    }
    if (hold_cycles > 255U)
    {
        hold_cycles = 255U;
    }
    pattern_cycles = (uint32_t)count * hold_cycles;
    if (pattern_cycles > 0xFFFFU)
    {
        pattern_cycles = 0xFFFFU;
    }

    return write_sram_waveform(samples, count) &&
           write_reg(REG_DDS_CONFIG, 0x0000) &&
           write_reg(REG_START_DLY, 0x0000) &&
           write_reg(REG_START_ADDR, ADDR_12BIT_FIELD(0)) &&
           write_reg(REG_STOP_ADDR, ADDR_12BIT_FIELD(count - 1U)) &&
           write_reg(REG_PAT_TIMEBASE, (uint16_t)((hold_cycles << 8) | 0x0011U)) &&
           write_reg(REG_PAT_PERIOD, (uint16_t)pattern_cycles) &&
           write_reg(REG_PAT_TYPE, 0x0001) &&
           write_reg(REG_WAV_CONFIG, WAV_CONFIG_WAVE_RAM);
}

void AD9102_IO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();

    pin_set(AD9102_PIN_SCLK | AD9102_PIN_MOSI | AD9102_PIN_TRIGGER, GPIO_PIN_RESET);
    pin_set(AD9102_PIN_CS_N | AD9102_PIN_RESET_N, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = AD9102_PIN_SCLK | AD9102_PIN_MOSI |
                          AD9102_PIN_CS_N | AD9102_PIN_RESET_N |
                          AD9102_PIN_TRIGGER;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AD9102_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = AD9102_PIN_MISO;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(AD9102_GPIO_PORT, &GPIO_InitStruct);

    pin_set(AD9102_PIN_CS_N | AD9102_PIN_RESET_N, GPIO_PIN_SET);
    pin_set(AD9102_PIN_TRIGGER | AD9102_PIN_SCLK, GPIO_PIN_RESET);
}

bool AD9102_Init(void)
{
    uint16_t dummy = 0;

    pin_set(AD9102_PIN_RESET_N, GPIO_PIN_RESET);
    HAL_Delay(2U);
    pin_set(AD9102_PIN_RESET_N, GPIO_PIN_SET);
    HAL_Delay(10U);

    (void)read_reg(REG_SPICONFIG, &dummy);
    return AD9102_Configure(AD9102_WAVE_SINE, AD9102_DEFAULT_FREQ_HZ);
}

bool AD9102_Configure(ad9102_wave_t wave, uint32_t freq_hz)
{
    bool ok = true;

    ok = ok && write_reg(REG_PAT_STATUS, 0x0000);
    ok = ok && write_reg(REG_DACDOF, 0x0000);
    ok = ok && write_reg(REG_DAC_DGAIN, DAC_12BIT_FIELD(AD9102_DEFAULT_GAIN));
    ok = ok && write_dds_frequency(freq_hz);

    switch (wave)
    {
    case AD9102_WAVE_SINE:
        ok = ok && write_reg(REG_DDS_CONFIG, 0x0000);
        ok = ok && write_reg(REG_TW_RAM_CONFIG, 0x0000);
        ok = ok && write_reg(REG_WAV_CONFIG, WAV_CONFIG_DDS_CONTINUOUS);
        break;
    case AD9102_WAVE_SQUARE:
        ok = ok && configure_sram_waveform(s_square, 2U, freq_hz);
        break;
    case AD9102_WAVE_TRIANGLE:
        ok = ok && configure_sram_waveform(s_triangle, 32U, freq_hz);
        break;
    case AD9102_WAVE_ARBITRARY:
        ok = ok && configure_sram_waveform(s_stair, 16U, freq_hz);
        break;
    default:
        return false;
    }

    ok = ok && write_reg(REG_PAT_STATUS, 0x0001);
    ok = ok && ram_update();

    pin_set(AD9102_PIN_TRIGGER, GPIO_PIN_SET);
    delay_cycles(120U);
    pin_set(AD9102_PIN_TRIGGER, GPIO_PIN_RESET);
    delay_cycles(120U);

    if (ok)
    {
        s_wave = wave;
        s_freq_hz = freq_hz;
    }
    return ok;
}

bool AD9102_SetMode(ad9102_wave_t wave)
{
    return AD9102_Configure(wave, s_freq_hz);
}

ad9102_wave_t AD9102_GetMode(void)
{
    return s_wave;
}

uint32_t AD9102_GetFreqHz(void)
{
    return s_freq_hz;
}
