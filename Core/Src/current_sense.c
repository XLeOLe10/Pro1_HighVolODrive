#include "current_sense.h"
#include "adc.h"
#include "main.h"

void CurrentSense_Init(void)
{
  /* H7 calibration must be performed after HAL_ADC_Init and while disabled. */
  if (HAL_ADCEx_Calibration_Start(&hadc1,
                                 ADC_CALIB_OFFSET_LINEARITY,
                                 ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ADCEx_Calibration_Start(&hadc2,
                                 ADC_CALIB_OFFSET_LINEARITY,
                                 ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ADCEx_Calibration_Start(&hadc3,
                                 ADC_CALIB_OFFSET_LINEARITY,
                                 ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc3, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
}

void CurrentSense_Start(void)
{
  /* Arm all three injected groups before TIM1 channel 4 produces TRGO2. */
  if (HAL_ADCEx_InjectedStart(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ADCEx_InjectedStart(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ADCEx_InjectedStart(&hadc3) != HAL_OK)
  {
    Error_Handler();
  }
}

uint8_t CurrentSense_HasNewSample(void)
{
  if ((__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_JEOS) == 0U) ||
      (__HAL_ADC_GET_FLAG(&hadc2, ADC_FLAG_JEOS) == 0U) ||
      (__HAL_ADC_GET_FLAG(&hadc3, ADC_FLAG_JEOS) == 0U))
  {
    return 0U;
  }

  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc3, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  return 1U;
}

void CurrentSense_GetRaw(uint16_t *iu,
                         uint16_t *iv,
                         uint16_t *iw,
                         uint16_t *vbus)
{
  *iu = (uint16_t)hadc1.Instance->JDR1;
  *iw = (uint16_t)hadc3.Instance->JDR1;
  *iv = (uint16_t)hadc2.Instance->JDR1;
  *vbus = (uint16_t)hadc3.Instance->JDR2;
}
