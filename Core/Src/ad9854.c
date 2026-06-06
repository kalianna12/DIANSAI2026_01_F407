#include "ad9854.h"

/*
 * AD9854 module reference flow:
 * 20 MHz module crystal, PLL x15, SYSCLK = 300 MHz.
 */
#define AD9854_CLK_SET         15U
#define AD9854_FREQ_MULT_ULONG 938250UL

/*
 * Pin mapping (all control pins on GPIOA):
 *   RST  -> PA0    UCLK -> PA1    WR   -> PA2
 *   RD   -> PA3    OSK  -> PA4    FSK  -> PA5
 *
 * GPIOC parallel bus:
 *   A[0:5] -> PC[0:5]    D[0:7] -> PC[6:13]
 */

static uint8_t s_freq_word[6];

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- > 0U)
    {
        __NOP();
    }
}

static void update_pulse(void)
{
    HAL_GPIO_WritePin(GPIOA, AD9854_UCLK_PIN, GPIO_PIN_SET);
    delay_cycles(20U);
    HAL_GPIO_WritePin(GPIOA, AD9854_UCLK_PIN, GPIO_PIN_RESET);
    delay_cycles(20U);
}

static void write_byte(uint8_t addr, uint8_t data)
{
    /* A[5:0] on PC[5:0], D[7:0] on PC[13:6] */
    uint32_t value = ((uint32_t)(addr & 0x3FU) << 0)
                   | ((uint32_t)(data & 0xFFU) << 6);

    GPIOC->BSRR = (value & 0x3FFFU) | (((~value) & 0x3FFFU) << 16);
    delay_cycles(20U);

    HAL_GPIO_WritePin(GPIOA, AD9854_WR_PIN, GPIO_PIN_RESET);
    delay_cycles(20U);
    HAL_GPIO_WritePin(GPIOA, AD9854_WR_PIN, GPIO_PIN_SET);
    delay_cycles(20U);
}

static void freq_convert(uint32_t freq_hz)
{
    uint32_t freq_buf;
    uint8_t freq_bytes[4];

    freq_bytes[0] = (uint8_t)freq_hz;
    freq_bytes[1] = (uint8_t)(freq_hz >> 8);
    freq_bytes[2] = (uint8_t)(freq_hz >> 16);
    freq_bytes[3] = (uint8_t)(freq_hz >> 24);

    freq_buf = AD9854_FREQ_MULT_ULONG * freq_bytes[0];
    s_freq_word[0] = (uint8_t)freq_buf;
    freq_buf >>= 8;

    freq_buf += AD9854_FREQ_MULT_ULONG * freq_bytes[1];
    s_freq_word[1] = (uint8_t)freq_buf;
    freq_buf >>= 8;

    freq_buf += AD9854_FREQ_MULT_ULONG * freq_bytes[2];
    s_freq_word[2] = (uint8_t)freq_buf;
    freq_buf >>= 8;

    freq_buf += AD9854_FREQ_MULT_ULONG * freq_bytes[3];
    s_freq_word[3] = (uint8_t)freq_buf;
    freq_buf >>= 8;

    s_freq_word[4] = (uint8_t)freq_buf;
    s_freq_word[5] = (uint8_t)(freq_buf >> 8);
}

void AD9854_IO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Reset all control pins to low */
    HAL_GPIO_WritePin(GPIOA,
                      AD9854_RST_PIN | AD9854_UCLK_PIN | AD9854_WR_PIN |
                      AD9854_RD_PIN | AD9854_OSK_PIN | AD9854_FSK_PIN,
                      GPIO_PIN_RESET);
    /* Reset all GPIOC bus pins to low */
    GPIOC->BSRR = 0x3FFF0000UL;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    /* Control pins: PA0-PA5 */
    GPIO_InitStruct.Pin = AD9854_RST_PIN | AD9854_UCLK_PIN | AD9854_WR_PIN |
                          AD9854_RD_PIN | AD9854_OSK_PIN | AD9854_FSK_PIN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Data/Address bus: PC0-PC13 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
                          GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                          GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* WR/RD inactive high, UCLK low */
    HAL_GPIO_WritePin(GPIOA, AD9854_WR_PIN | AD9854_RD_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, AD9854_UCLK_PIN, GPIO_PIN_RESET);
}

void AD9854_InitSingle(void)
{
    HAL_GPIO_WritePin(GPIOA, AD9854_WR_PIN | AD9854_RD_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, AD9854_UCLK_PIN, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOA, AD9854_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(2U);
    HAL_GPIO_WritePin(GPIOA, AD9854_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(2U);

    write_byte(0x1D, 0x00);
    write_byte(0x1E, AD9854_CLK_SET);
    write_byte(0x1F, 0x00);
    write_byte(0x20, 0x60);

    update_pulse();
}

void AD9854_SetSine(uint32_t freq_hz, uint16_t amplitude)
{
    uint8_t addr = 0x04;

    if (amplitude > AD9854_FULL_SCALE_AMP)
    {
        amplitude = AD9854_FULL_SCALE_AMP;
    }

    freq_convert(freq_hz);

    for (int count = 6; count > 0;)
    {
        write_byte(addr++, s_freq_word[--count]);
    }

    write_byte(0x21, (uint8_t)(amplitude >> 8));
    write_byte(0x22, (uint8_t)(amplitude & 0xFFU));
    write_byte(0x23, (uint8_t)(amplitude >> 8));
    write_byte(0x24, (uint8_t)(amplitude & 0xFFU));

    update_pulse();
}

void AD9854_Stop(void)
{
    write_byte(0x21, 0x00);
    write_byte(0x22, 0x00);
    write_byte(0x23, 0x00);
    write_byte(0x24, 0x00);
    update_pulse();
}
