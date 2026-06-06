/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ad9854.h"
#include "stm_adc.h"
#include "stm_spi_link.h"
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

static uint8_t last_k0 = 1;
static uint8_t last_k1 = 1;
static uint32_t current_freq_hz = AD9854_DEFAULT_FREQ_HZ;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void status_led_on(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
}

static void status_led_off(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
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

static void startup_blink(void)
{
    for (int i = 0; i < 3; i++)
    {
        status_led_on();
        HAL_Delay(120);
        status_led_off();
        HAL_Delay(120);
    }
}

static void ad9854_start_default(void)
{
    current_freq_hz = AD9854_DEFAULT_FREQ_HZ;
    AD9854_SetSine(current_freq_hz, AD9854_FULL_SCALE_AMP);
    STM_SPI_Link_SetCurrentFreq(current_freq_hz);
    status_led_on();
}

static void ad9854_apply_frequency(uint32_t freq_hz)
{
    current_freq_hz = clamp_freq(freq_hz);
    AD9854_SetSine(current_freq_hz, AD9854_FULL_SCALE_AMP);
    STM_SPI_Link_SetCurrentFreq(current_freq_hz);
    status_led_on();
}

static void key_feedback_blink(void)
{
    status_led_off();
    HAL_Delay(80);
    status_led_on();
}

static void scan_keys(void)
{
    uint8_t k0 = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4);  // K0, pressed = 0
    uint8_t k1 = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3);  // K1, pressed = 0

    // K0: -1 MHz local test, amplitude remains full scale.
    if (last_k0 == 1 && k0 == 0)
    {
        HAL_Delay(20);

        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET)
        {
            ad9854_apply_frequency(current_freq_hz - 1000000UL);
            key_feedback_blink();
        }
    }

    // K1: +1 MHz local test, amplitude remains full scale.
    if (last_k1 == 1 && k1 == 0)
    {
        HAL_Delay(20);

        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET)
        {
            ad9854_apply_frequency(current_freq_hz + 1000000UL);
            key_feedback_blink();
        }
    }

    last_k0 = k0;
    last_k1 = k1;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */

  startup_blink();
  STM_ADC_Init();
  AD9854_IO_Init();
  HAL_Delay(500);
  AD9854_InitSingle();
  ad9854_start_default();
  STM_SPI_Link_Init(current_freq_hz);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t spi_freq_hz = 0;
    if (STM_SPI_Link_Poll(&spi_freq_hz))
    {
        ad9854_apply_frequency(spi_freq_hz);
        key_feedback_blink();
    }
    scan_keys();

    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

  while (1)
  {
    // Fast PA7 (status LED) blink means code entered Error_Handler.
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_7);

    for (volatile uint32_t i = 0; i < 800000; i++)
    {
    }
  }

  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number,
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */

  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
