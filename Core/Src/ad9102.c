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
#define ADDR_12BIT_FIELD(v)       ((uint16_t)(((uint16_t)(v) & 0x0FFFU) << 4))
#define SRAM_TEST_POINTS          1024U
#define SRAM_TEST_PERIOD          (SRAM_TEST_POINTS - 1U)

static ad9102_wave_t s_wave = AD9102_WAVE_SINE;
static uint32_t s_freq_hz = AD9102_DEFAULT_FREQ_HZ;
static uint16_t s_amplitude = AD9102_DEFAULT_AMP;
static uint32_t s_actual_freq_hz = AD9102_DEFAULT_FREQ_HZ;

static const uint16_t s_reg_add[66] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x001f,
    0x0020, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028,
    0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 0x0030,
    0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x003e,
    0x003f, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0047,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x001e, 0x001d,
};

static const uint16_t s_ad9102_sram_regval[66] = {
    0x0000, 0x0e00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x1f00, 0x0000, 0x0000, 0x0000,
    0x000E, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3030, 0x0111,
    0xffff, 0x0000, 0x0101, 0x0003, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x4000, 0x0000, 0x0200, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0fa0, 0x0000, 0x3ff0, 0x0100,
    0x0001, 0x0001,
};

static int16_t s_sram_wave[4096];

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

static void stop_pattern(void)
{
    pin_set(AD9102_PIN_TRIGGER, GPIO_PIN_SET);
    delay_cycles(120U);
}

static void start_pattern(void)
{
    pin_set(AD9102_PIN_TRIGGER, GPIO_PIN_RESET);
    delay_cycles(120U);
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

static bool write_official_regs(const uint16_t *regs)
{
    if (regs == NULL)
    {
        return false;
    }

    for (uint16_t i = 0; i < 66U; i++)
    {
        if (!write_reg(s_reg_add[i], regs[i]))
        {
            return false;
        }
    }
    return true;
}

static bool write_sram_waveform(const int16_t *samples)
{
    if (samples == NULL)
    {
        return false;
    }

    if (!write_reg(REG_PAT_STATUS, 0x0004))
    {
        return false;
    }
    for (uint16_t i = 0; i < 4096U; i++)
    {
        int32_t shifted = ((int32_t)samples[i]) << 2;
        if (!write_reg((uint16_t)(REG_SRAM_DATA + i), (uint16_t)shifted))
        {
            return false;
        }
    }
    return write_reg(REG_PAT_STATUS, 0x0000);
}

static void build_sram_wave(ad9102_wave_t wave)
{
    for (uint16_t i = 0; i < 4096U; i++)
    {
        switch (wave)
        {
        case AD9102_WAVE_SQUARE:
            s_sram_wave[i] = ((i % SRAM_TEST_POINTS) < (SRAM_TEST_POINTS / 2U)) ? -512 : 511;
            break;
        case AD9102_WAVE_TRIANGLE:
            if ((i % SRAM_TEST_POINTS) < (SRAM_TEST_POINTS / 2U))
            {
                uint16_t phase = i % SRAM_TEST_POINTS;
                s_sram_wave[i] = (int16_t)((int32_t)-1024 + ((int32_t)phase * 2047) / ((SRAM_TEST_POINTS / 2U) - 1U));
            }
            else
            {
                uint16_t phase = (i % SRAM_TEST_POINTS) - (SRAM_TEST_POINTS / 2U);
                s_sram_wave[i] = (int16_t)((int32_t)1023 - ((int32_t)phase * 2047) / ((SRAM_TEST_POINTS / 2U) - 1U));
            }
            break;
        case AD9102_WAVE_ARBITRARY:
            s_sram_wave[i] = (int16_t)((int32_t)-1024 + ((int32_t)(i % SRAM_TEST_POINTS) * 2047) / SRAM_TEST_PERIOD);
            break;
        default:
            s_sram_wave[i] = 0;
            break;
        }
    }
}

static bool configure_sram_waveform(ad9102_wave_t wave, uint32_t *actual_freq_hz)
{
    bool ok = true;

    stop_pattern();
    ok = ok && write_reg(REG_PAT_STATUS, 0x0000);
    ok = ok && write_reg(REG_WAV_CONFIG, 0x0000);
    ok = ok && write_reg(REG_DDS_CONFIG, 0x0000);
    ok = ok && write_reg(REG_TW_RAM_CONFIG, 0x0000);
    ok = ok && write_reg(REG_PAT_TYPE, 0x0000);
    ok = ok && write_reg(REG_PAT_TIMEBASE, 0x0000);
    ok = ok && write_reg(REG_PAT_PERIOD, 0x0000);
    ok = ok && write_reg(REG_START_DLY, 0x0000);
    ok = ok && write_reg(REG_START_ADDR, 0x0000);
    ok = ok && write_reg(REG_STOP_ADDR, 0x0000);
    ok = ok && ram_update();

    build_sram_wave(wave);
    if (actual_freq_hz != NULL)
    {
        *actual_freq_hz = AD9102_DAC_CLK_HZ / SRAM_TEST_POINTS;
    }

    ok = ok && write_sram_waveform(s_sram_wave);
    ok = ok && write_official_regs(s_ad9102_sram_regval);
    ok = ok && write_reg(REG_PAT_PERIOD, SRAM_TEST_PERIOD);
    ok = ok && write_reg(REG_START_DLY, 0x0000);
    ok = ok && write_reg(REG_START_ADDR, ADDR_12BIT_FIELD(0U));
    ok = ok && write_reg(REG_STOP_ADDR, ADDR_12BIT_FIELD(SRAM_TEST_PERIOD));
    ok = ok && write_reg(REG_DDS_CONFIG, 0x0000);
    ok = ok && write_reg(REG_TW_RAM_CONFIG, 0x0000);
    ok = ok && write_reg(REG_WAV_CONFIG, WAV_CONFIG_WAVE_RAM);
    ok = ok && write_reg(REG_PAT_STATUS, 0x0001);
    ok = ok && ram_update();
    start_pattern();
    return ok;
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

    pin_set(AD9102_PIN_CS_N | AD9102_PIN_RESET_N | AD9102_PIN_TRIGGER, GPIO_PIN_SET);
    pin_set(AD9102_PIN_SCLK, GPIO_PIN_RESET);
}

bool AD9102_Init(void)
{
    uint16_t dummy = 0;

    pin_set(AD9102_PIN_RESET_N, GPIO_PIN_RESET);
    HAL_Delay(2U);
    pin_set(AD9102_PIN_RESET_N, GPIO_PIN_SET);
    HAL_Delay(10U);

    (void)read_reg(REG_SPICONFIG, &dummy);
    return AD9102_Configure(AD9102_WAVE_SINE, AD9102_DEFAULT_FREQ_HZ, AD9102_DEFAULT_AMP);
}

bool AD9102_Configure(ad9102_wave_t wave, uint32_t freq_hz, uint16_t amplitude)
{
    bool ok = true;
    uint32_t actual_freq_hz = freq_hz;

    if (amplitude > 0x07FFU)
    {
        amplitude = 0x07FFU;
    }

    stop_pattern();
    ok = ok && write_reg(REG_PAT_STATUS, 0x0000);
    ok = ok && write_reg(REG_DACDOF, 0x0000);
    ok = ok && write_reg(REG_DAC_DGAIN, DAC_12BIT_FIELD(amplitude));
    ok = ok && write_dds_frequency(freq_hz);

    switch (wave)
    {
    case AD9102_WAVE_SINE:
        ok = ok && write_reg(REG_DDS_CONFIG, 0x0000);
        ok = ok && write_reg(REG_TW_RAM_CONFIG, 0x0000);
        ok = ok && write_reg(REG_WAV_CONFIG, WAV_CONFIG_DDS_CONTINUOUS);
        actual_freq_hz = freq_hz;
        break;
    case AD9102_WAVE_SQUARE:
        ok = ok && configure_sram_waveform(wave, &actual_freq_hz);
        break;
    case AD9102_WAVE_TRIANGLE:
        ok = ok && configure_sram_waveform(wave, &actual_freq_hz);
        break;
    case AD9102_WAVE_ARBITRARY:
        ok = ok && configure_sram_waveform(wave, &actual_freq_hz);
        break;
    default:
        return false;
    }

    if (wave == AD9102_WAVE_SINE)
    {
        ok = ok && write_reg(REG_PAT_STATUS, 0x0001);
        ok = ok && ram_update();
        start_pattern();
    }

    if (ok)
    {
        s_wave = wave;
        s_freq_hz = actual_freq_hz;
        s_actual_freq_hz = actual_freq_hz;
        s_amplitude = amplitude;
    }
    return ok;
}

bool AD9102_SetMode(ad9102_wave_t wave)
{
    return AD9102_Configure(wave, s_freq_hz, s_amplitude);
}

bool AD9102_SetFrequency(uint32_t freq_hz)
{
    return AD9102_Configure(s_wave, freq_hz, s_amplitude);
}

bool AD9102_SetAmplitude(uint16_t amplitude)
{
    if (amplitude > 0x07FFU)
    {
        amplitude = 0x07FFU;
    }
    if (!write_reg(REG_DAC_DGAIN, DAC_12BIT_FIELD(amplitude)))
    {
        return false;
    }
    if (!ram_update())
    {
        return false;
    }
    s_amplitude = amplitude;
    return true;
}

ad9102_wave_t AD9102_GetMode(void)
{
    return s_wave;
}

uint32_t AD9102_GetFreqHz(void)
{
    return s_actual_freq_hz;
}

uint16_t AD9102_GetAmplitude(void)
{
    return s_amplitude;
}

/* ---------- AFSK timer-based transmission (100bps) ---------- */

#define AFSK_MARK_HZ   1200U
#define AFSK_SPACE_HZ  2200U
#define AFSK_BPS        100U
#define AFSK_BIT_MS    (1000U / AFSK_BPS)  /* 10 ms */

/* We use TIM2 for AFSK bit clock. ISR lives here. */
static volatile bool s_afsk_busy = false;
static uint8_t  s_afsk_tx_buf[80];
static uint16_t s_afsk_bit_count;
static uint16_t s_afsk_bit_idx;

static uint8_t crc8_afsk(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* AFSK byte order: LSB first per byte */
static void build_afsk_frame(uint8_t addr, const uint8_t *data, uint8_t len)
{
    /* Preamble: 4 x 0xAA = 32 bits alternating 0101..., ~320ms @100bps */
    s_afsk_tx_buf[0] = 0xAA;
    s_afsk_tx_buf[1] = 0xAA;
    s_afsk_tx_buf[2] = 0xAA;
    s_afsk_tx_buf[3] = 0xAA;
    /* Start flag: 0x7E */
    s_afsk_tx_buf[4] = 0x7E;
    /* Address */
    s_afsk_tx_buf[5] = addr;
    /* Length */
    s_afsk_tx_buf[6] = len;
    /* Data */
    for (uint8_t i = 0; i < len; i++) {
        s_afsk_tx_buf[7 + i] = data[i];
    }
    /* CRC-8 over addr + len + data */
    uint8_t crc = crc8_afsk(&s_afsk_tx_buf[5], (uint8_t)(2 + len));
    s_afsk_tx_buf[7 + len] = crc;

    s_afsk_bit_count = (uint16_t)((8U + len) * 8U);
    s_afsk_bit_idx = 0;
}

bool AD9102_StartAfsk(uint8_t addr, const uint8_t *data, uint8_t len)
{
    if (len == 0 || data == NULL) {
        return false;
    }
    if (s_afsk_busy) {
        return false;
    }
    s_afsk_busy = true;

    /* Ensure AD9102 is in DDS sine continuous mode, not SRAM */
    write_reg(REG_PAT_STATUS, 0x0000);   /* stop pattern */
    write_reg(REG_DDS_CONFIG, 0x0000);
    write_reg(REG_TW_RAM_CONFIG, 0x0000);
    write_reg(REG_WAV_CONFIG, WAV_CONFIG_DDS_CONTINUOUS);
    write_reg(REG_PAT_STATUS, 0x0001);   /* run */
    ram_update();

    build_afsk_frame(addr, data, len);

    /* Enable TIM2 clock and configure for AFSK bit rate */
    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0;
    TIM2->PSC = 8399;   /* 84 MHz / 8400 = 10 kHz */
    TIM2->ARR = 99;     /* 10 kHz / 100 = 100 Hz → 10 ms */
    TIM2->CNT = 0;
    TIM2->DIER = TIM_DIER_UIE;  /* Enable update interrupt */
    TIM2->CR1 = TIM_CR1_CEN;    /* Start timer */

    /* Set NVIC priority for TIM2 (lower number = higher priority) */
    NVIC_SetPriority(TIM2_IRQn, 2);
    NVIC_EnableIRQ(TIM2_IRQn);

    return true;
}

/* TIM2 interrupt handler — called every 10 ms to output next AFSK bit */
void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) == 0U) {
        return;
    }
    TIM2->SR = ~TIM_SR_UIF;

    if (s_afsk_bit_idx < s_afsk_bit_count) {
        uint16_t byte_idx = s_afsk_bit_idx >> 3;
        uint8_t  bit_pos  = (uint8_t)(s_afsk_bit_idx & 0x07U);
        uint8_t  bit_val  = (s_afsk_tx_buf[byte_idx] >> bit_pos) & 0x01U;

        /* Direct DDS frequency update (lightweight, no SRAM reconfig) */
        write_dds_frequency(bit_val ? AFSK_MARK_HZ : AFSK_SPACE_HZ);
        ram_update();

        s_afsk_bit_idx++;
    } else {
        /* All bits sent — stop timer */
        TIM2->DIER = 0;
        TIM2->CR1 = 0;
        s_afsk_busy = false;
    }
}
