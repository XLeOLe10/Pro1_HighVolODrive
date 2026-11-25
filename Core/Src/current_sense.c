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

/* ADC CHECK TRACE BEGIN: tested from this production block on the host. */
#if AXIS_COMMAND_BENCH_ENABLE
/* TIM1 is the sole runtime writer. This private scratch is valid only for
 * the just-failed check; app_runtime freezes it after its normal fault gate. */
typedef struct {
  uint32_t first_missing_adc;
  uint32_t missing_isr;
  uint32_t check_cycles;
  uint32_t check_interval_cycles;
  uint32_t since_success_cycles;
  uint32_t tim_phase;
  uint32_t tim_sr;
} CurrentSenseMissingCheck;
static CurrentSenseMissingCheck missing_check;
static uint32_t previous_check_cycles;
static uint32_t previous_check_valid;
static uint32_t previous_success_check_cycles;
static uint32_t previous_success_check_valid;
volatile CurrentSenseFaultTrace current_sense_fault_trace;

static void RecordMissingCheck(uint32_t adc, uint32_t isr, uint32_t check_cycles)
{
  uint32_t dir_before;
  uint32_t count;
  uint32_t dir_after;
  /* Read after the missing decision. Do not pretend this is an atomic timer
   * phase or reread ADC flags that may already have changed since the check. */
  dir_before = TIM1->CR1;
  count = TIM1->CNT;
  dir_after = TIM1->CR1;
  missing_check.first_missing_adc = adc;
  missing_check.missing_isr = isr;
  missing_check.check_cycles = check_cycles;
  missing_check.check_interval_cycles = previous_check_valid != 0U ?
      (uint32_t)(check_cycles - previous_check_cycles) : UINT32_MAX;
  missing_check.since_success_cycles = previous_success_check_valid != 0U ?
      (uint32_t)(check_cycles - previous_success_check_cycles) : UINT32_MAX;
  missing_check.tim_phase = (count & 0xFFFFU) |
      ((dir_after & TIM_CR1_DIR) != 0U ? (1UL << 16) : 0U) |
      (((dir_before ^ dir_after) & TIM_CR1_DIR) != 0U ? (1UL << 31) : 0U);
  missing_check.tim_sr = TIM1->SR;
  previous_check_cycles = check_cycles;
  previous_check_valid = 1U;
}

void CurrentSense_CaptureFault(uint32_t body_entry_cycles,
                              uint32_t previous_body_entry_cycles,
                              uint32_t previous_body_cycles,
                              uint32_t previous_body_valid,
                              uint32_t command_seq)
{
  if ((current_sense_fault_trace.captured != 0U) ||
      (missing_check.first_missing_adc == 0U)) return;
  current_sense_fault_trace.first_missing_adc = missing_check.first_missing_adc;
  current_sense_fault_trace.missing_isr = missing_check.missing_isr;
  current_sense_fault_trace.check_interval_cycles = missing_check.check_interval_cycles;
  current_sense_fault_trace.since_success_cycles = missing_check.since_success_cycles;
  current_sense_fault_trace.body_entry_interval_cycles = previous_body_valid != 0U ?
      (uint32_t)(body_entry_cycles - previous_body_entry_cycles) : UINT32_MAX;
  current_sense_fault_trace.entry_to_check_cycles =
      (uint32_t)(missing_check.check_cycles - body_entry_cycles);
  current_sense_fault_trace.previous_body_cycles =
      previous_body_valid != 0U ? previous_body_cycles : UINT32_MAX;
  current_sense_fault_trace.tim_phase = missing_check.tim_phase;
  current_sense_fault_trace.tim_sr = missing_check.tim_sr;
  current_sense_fault_trace.command_seq = command_seq;
  __DMB();
  current_sense_fault_trace.captured = 1U;
}
#endif

uint8_t CurrentSense_HasNewSample(void)
{
#if AXIS_COMMAND_BENCH_ENABLE
  uint32_t check_cycles = DWT->CYCCNT;
  uint32_t isr;
  missing_check.first_missing_adc = 0U; /* No stale failure survives a new check. */
  /* Preserve the original ADC1/2/3 short circuit and success-only W1C order.
   * Each examined ISR is read exactly once; later ADCs stay unexamined. */
  isr = hadc1.Instance->ISR;
  if ((isr & ADC_FLAG_JEOS) == 0U)
  {
    RecordMissingCheck(1U, isr, check_cycles);
    return 0U;
  }
  isr = hadc2.Instance->ISR;
  if ((isr & ADC_FLAG_JEOS) == 0U)
  {
    RecordMissingCheck(2U, isr, check_cycles);
    return 0U;
  }
  isr = hadc3.Instance->ISR;
  if ((isr & ADC_FLAG_JEOS) == 0U)
  {
    RecordMissingCheck(3U, isr, check_cycles);
    return 0U;
  }
#else
  if ((__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_JEOS) == 0U) ||
      (__HAL_ADC_GET_FLAG(&hadc2, ADC_FLAG_JEOS) == 0U) ||
      (__HAL_ADC_GET_FLAG(&hadc3, ADC_FLAG_JEOS) == 0U))
  {
    return 0U;
  }
#endif

  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
  __HAL_ADC_CLEAR_FLAG(&hadc3, ADC_FLAG_JEOS | ADC_FLAG_JEOC);
#if AXIS_COMMAND_BENCH_ENABLE
  previous_check_cycles = check_cycles;
  previous_check_valid = 1U;
  previous_success_check_cycles = check_cycles;
  previous_success_check_valid = 1U;
#endif
  return 1U;
}
/* ADC CHECK TRACE END */

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
