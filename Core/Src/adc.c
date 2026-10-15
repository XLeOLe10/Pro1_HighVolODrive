#include "adc.h"

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
ADC_HandleTypeDef hadc3;

static void ADC_CommonInit(ADC_HandleTypeDef *hadc)
{
  hadc->Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc->Init.Resolution = ADC_RESOLUTION_12B;
  hadc->Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc->Init.EOCSelection = ADC_EOC_SEQ_CONV;
  hadc->Init.LowPowerAutoWait = DISABLE;
  hadc->Init.ContinuousConvMode = DISABLE;
  hadc->Init.NbrOfConversion = 1;
  hadc->Init.DiscontinuousConvMode = DISABLE;
  hadc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc->Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc->Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc->Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc->Init.OversamplingMode = DISABLE;
}

static void ADC_InjectedConfig(ADC_HandleTypeDef *hadc,
                               uint32_t channel,
                               uint32_t rank,
                               uint32_t rank_count)
{
  ADC_InjectionConfTypeDef config = {0};

  config.InjectedChannel = channel;
  config.InjectedRank = rank;
  /* CLKP=64 MHz and ADC async /4 gives 16 MHz. 64.5 cycles is 4.03 us,
     matching the G474 analogue acquisition time without overclocking ADC12. */
  config.InjectedSamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  config.InjectedSingleDiff = ADC_SINGLE_ENDED;
  config.InjectedOffsetNumber = ADC_OFFSET_NONE;
  config.InjectedOffset = 0;
  config.InjectedOffsetRightShift = DISABLE;
  config.InjectedOffsetSignedSaturation = DISABLE;
  config.InjectedNbrOfConversion = rank_count;
  config.InjectedDiscontinuousConvMode = DISABLE;
  config.AutoInjectedConv = DISABLE;
  config.QueueInjectedContext = DISABLE;
  config.ExternalTrigInjecConv = ADC_EXTERNALTRIGINJEC_T1_TRGO2;
  config.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;
  config.InjecOversamplingMode = DISABLE;

  if (HAL_ADCEx_InjectedConfigChannel(hadc, &config) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_ADC1_Init(void)
{
  ADC_MultiModeTypeDef multimode = {0};

  hadc1.Instance = ADC1;
  ADC_CommonInit(&hadc1);
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  ADC_InjectedConfig(&hadc1, ADC_CHANNEL_16, ADC_INJECTED_RANK_1, 2U); /* PA0: U */
  ADC_InjectedConfig(&hadc1, ADC_CHANNEL_10, ADC_INJECTED_RANK_2, 2U); /* PC0: W */
}

void MX_ADC2_Init(void)
{
  hadc2.Instance = ADC2;
  ADC_CommonInit(&hadc2);
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  ADC_InjectedConfig(&hadc2, ADC_CHANNEL_19, ADC_INJECTED_RANK_1, 1U); /* PA5: V */
}

void MX_ADC3_Init(void)
{
  hadc3.Instance = ADC3;
  ADC_CommonInit(&hadc3);
  if (HAL_ADC_Init(&hadc3) != HAL_OK)
  {
    Error_Handler();
  }

  ADC_InjectedConfig(&hadc3, ADC_CHANNEL_1, ADC_INJECTED_RANK_1, 1U); /* PC3: VBUS */
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *adcHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  if (adcHandle->Instance == ADC1)
  {
    /* CLKP is the 64 MHz per_ck in the retained 200 MHz clock tree. */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
  }

  if ((adcHandle->Instance == ADC1) || (adcHandle->Instance == ADC2))
  {
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    if (adcHandle->Instance == ADC1)
    {
      GPIO_InitStruct.Pin = GPIO_PIN_0;
      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
      GPIO_InitStruct.Pin = GPIO_PIN_0;
      HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    }
    else
    {
      GPIO_InitStruct.Pin = GPIO_PIN_5;
      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
  }
  else if (adcHandle->Instance == ADC3)
  {
    __HAL_RCC_ADC3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LQFP100 pin 18 is the dedicated PC3_C analogue pad. Keep its internal
       switch to the digital PC3 pad open, as required for direct ADC3_INP1. */
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC3,
                                  SYSCFG_SWITCH_PC3_OPEN);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adcHandle)
{
  if (adcHandle->Instance == ADC1)
  {
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0);
  }
  else if (adcHandle->Instance == ADC2)
  {
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5);
  }
  else if (adcHandle->Instance == ADC3)
  {
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_3);
  }
}
