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
#include "adc.h"
#include "usart.h"
#include "tim.h"
#include "spi.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "board_config.h"
#include "current_sense.h"
#include "current_monitor.h"
#include "open_foc.h"
#include "axis_state.h"
#include "ad2s1210.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CURRENT_LOOP_UQ_FF_NORM          0.0f
#define RESOLVER_TEST_UQ_NORM            0.02f
#define SPEED_LOOP_TARGET_RPM            50.0f
#define CURRENT_TELEMETRY_PERIOD_MS        5U
/* Closed-loop operation requires healthy resolver feedback. Open-loop test
   deliberately does not require rotor feedback. */
#define RESOLVER_DYNAMIC_TEST_LOCKOUT     0U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint8_t power_pwm_started = 0U;
static volatile uint8_t power_stage_enabled = 0U;
volatile uint8_t resolver_dynamic_test_lockout_active =
    RESOLVER_DYNAMIC_TEST_LOCKOUT;
volatile uint32_t foc_isr_last_cycles = 0U;
volatile uint32_t foc_isr_max_cycles = 0U;
volatile uint32_t foc_isr_budget_cycles = 0U;
volatile uint32_t foc_isr_overrun_count = 0U;
volatile uint32_t current_telemetry_tx_count = 0U;
volatile uint32_t current_telemetry_drop_count = 0U;
static uint8_t current_telemetry_tx_buffer[96];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void BoardPowerStage_Init(void)
{
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_PinState inactive_level;

  BOARD_GATE_EN_GPIO_CLK_ENABLE();
  inactive_level = (BOARD_GATE_EN_ACTIVE_LEVEL == GPIO_PIN_SET) ?
                   GPIO_PIN_RESET : GPIO_PIN_SET;
  HAL_GPIO_WritePin(BOARD_GATE_EN_GPIO_PORT, BOARD_GATE_EN_PIN, inactive_level);

  GPIO_InitStruct.Pin = BOARD_GATE_EN_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BOARD_GATE_EN_GPIO_PORT, &GPIO_InitStruct);
#endif
}

static void BoardPWM_PinsSafeGPIO(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8 | GPIO_PIN_10, GPIO_PIN_RESET);

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_10;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}

#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
static void BoardPWM_ConnectTIM1(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_10;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}

#if (RESOLVER_DYNAMIC_TEST_LOCKOUT == 0U)
static void BoardPowerStage_Enable(void)
{
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
  HAL_GPIO_WritePin(BOARD_GATE_EN_GPIO_PORT,
                    BOARD_GATE_EN_PIN,
                    BOARD_GATE_EN_ACTIVE_LEVEL);
#endif
}
#endif

static void BoardPowerStage_Disable(void)
{
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
  GPIO_PinState inactive_level;

  inactive_level = (BOARD_GATE_EN_ACTIVE_LEVEL == GPIO_PIN_SET) ?
                   GPIO_PIN_RESET : GPIO_PIN_SET;
  HAL_GPIO_WritePin(BOARD_GATE_EN_GPIO_PORT,
                    BOARD_GATE_EN_PIN,
                    inactive_level);
#endif
}

static void MotorPWM_StartOutputs(void)
{
  /* The GPIO-enable mode keeps the driver disabled while these six enables
     are applied. Compare registers already contain the current FOC command. */
  if ((HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) ||
      (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) ||
      (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) ||
      (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) ||
      (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) != HAL_OK) ||
      (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3) != HAL_OK))
  {
    Error_Handler();
  }
}
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  BoardPowerStage_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_ADC3_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_SPI1_Init();
  /* CH1/2/3 pads are forced low until the current offsets are valid. TIM1_CH4
     is internal, so the ADC trigger and control interrupt continue to work. */
  BoardPWM_PinsSafeGPIO();
  /* USER CODE BEGIN 2 */
  CurrentSense_Init();
  CurrentMonitor_Init();
  AD2S1210_Init();
  foc_isr_last_cycles = 0U;
  foc_isr_max_cycles = 0U;
  foc_isr_overrun_count = 0U;
  foc_isr_budget_cycles = SystemCoreClock / 20000U;
  OpenFOC_Init();
  OpenFOC_SetUqVoltageNorm(CURRENT_LOOP_UQ_FF_NORM);
  /* Keep resolver commissioning deliberately low even though the current-loop
     feed-forward value is higher. */
  foc_uq_open_loop_norm = RESOLVER_TEST_UQ_NORM;
  OpenFOC_SetTargetRPM(SPEED_LOOP_TARGET_RPM);

  /* Arm ADC1/2/3 before starting internal TIM1_CH4/TRGO2. CH1-3 power outputs
     remain off until calibration completes and board_config permits them. */
  CurrentSense_Start();
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  {
    uint32_t last_resolver_tick = HAL_GetTick();
    uint32_t last_telemetry_tick = HAL_GetTick();

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - last_resolver_tick) >= 1U)
    {
      last_resolver_tick = now;
      AD2S1210_Run_1kHz();
    }

    if ((power_pwm_started == 0U) && (CurrentMonitor_IsReady() != 0U))
    {
#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
      MotorPWM_StartOutputs();
      power_pwm_started = 1U;
#endif
    }

    /* 200 Hz nonblocking CSV telemetry for speed-loop tuning.  Column order:
       speed_ref_rpm,speed_feedback_rpm */
    if ((uint32_t)(now - last_telemetry_tick) >=
        CURRENT_TELEMETRY_PERIOD_MS)
    {
      int telemetry_length;

      last_telemetry_tick = now;
      if (huart1.gState == HAL_UART_STATE_READY)
      {
        telemetry_length = snprintf(
            (char *)current_telemetry_tx_buffer,
            sizeof(current_telemetry_tx_buffer),
            "%.3f,%.3f\r\n",
            (double)foc_speed_ref_rpm,
            (double)foc_speed_feedback_rpm);
        if (telemetry_length > 0)
        {
          if (telemetry_length >= (int)sizeof(current_telemetry_tx_buffer))
          {
            telemetry_length = (int)sizeof(current_telemetry_tx_buffer) - 1;
          }
          if (HAL_UART_Transmit_IT(&huart1,
                                   current_telemetry_tx_buffer,
                                   (uint16_t)telemetry_length) == HAL_OK)
          {
            current_telemetry_tx_count++;
          }
          else
          {
            current_telemetry_drop_count++;
          }
        }
      }
      else
      {
        current_telemetry_drop_count++;
      }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 32;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  static uint8_t speed_loop_divider = 0U;
  uint32_t isr_start_cycles = 0U;
  uint32_t isr_elapsed_cycles;

  if (htim->Instance == TIM1)
  {
    if (ad2s_dwt_ready != 0U)
    {
      isr_start_cycles = DWT->CYCCNT;
    }

    AD2S1210_Run_20kHz();
    OpenFOC_SetRotorFeedback(ad2s_position_rad,
                             ad2s_speed_rpm_filt,
                             AD2S1210_IsPositionDataValid() &&
                             AD2S1210_IsFeedbackValid());

    CurrentMonitor_Run_20kHz(foc_theta);
    OpenFOC_SetCurrentFeedback(monitor_id_a,
                               monitor_iq_a,
                               CurrentMonitor_IsReady());

    speed_loop_divider++;
    if (speed_loop_divider >= 20U)
    {
      speed_loop_divider = 0U;
      OpenFOC_Ramp_1ms();
    }

    OpenFOC_Run_20kHz();

#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
#if (RESOLVER_DYNAMIC_TEST_LOCKOUT != 0U)
    resolver_dynamic_test_lockout_active = 1U;
    foc_run_request = 0U;
    if (power_stage_enabled != 0U)
    {
      BoardPowerStage_Disable();
      BoardPWM_PinsSafeGPIO();
      power_stage_enabled = 0U;
    }
#else
    /* The ISR is the sole owner of runtime gate enable/disable. */
    if ((power_pwm_started != 0U) && (foc_run_request != 0U) &&
        (foc_axis_output_enabled != 0U))
    {
      if (power_stage_enabled == 0U)
      {
        BoardPWM_ConnectTIM1();
        BoardPowerStage_Enable();
        power_stage_enabled = 1U;
      }
    }
    else if (power_stage_enabled != 0U)
    {
      BoardPowerStage_Disable();
      BoardPWM_PinsSafeGPIO();
      power_stage_enabled = 0U;
    }
#endif
#endif

    foc_axis_power_stage_enabled = power_stage_enabled;

    if (ad2s_dwt_ready != 0U)
    {
      isr_elapsed_cycles = (uint32_t)(DWT->CYCCNT - isr_start_cycles);
      foc_isr_last_cycles = isr_elapsed_cycles;
      if (isr_elapsed_cycles > foc_isr_max_cycles)
      {
        foc_isr_max_cycles = isr_elapsed_cycles;
      }
      if ((foc_isr_budget_cycles != 0U) &&
          (isr_elapsed_cycles > foc_isr_budget_cycles))
      {
        foc_isr_overrun_count++;
      }
    }
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
