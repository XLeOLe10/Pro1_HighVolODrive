#include "main.h"
#include "app_runtime.h"
#include "app_policy.h"
#include "axis_status.h"
#include "axis_command.h"
#include "board_config.h"
#include "board_safety.h"
#include "current_sense.h"
#include "current_monitor.h"
#include "ad2s1210.h"
#include "open_foc.h"
#include "tim.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include <stdio.h>

#define CURRENT_LOOP_UQ_FF_NORM      0.0f
#define RESOLVER_TEST_UQ_NORM        0.02f
#define RESOLVER_DYNAMIC_TEST_LOCKOUT 0U

/* Device fault bits remain diagnostic-only. Transport/age validity is enforced.
 * No automatic fault clear or automatic restart in this initial integration. */
volatile uint8_t resolver_dynamic_test_lockout_active = RESOLVER_DYNAMIC_TEST_LOCKOUT;
volatile uint32_t foc_isr_last_cycles = 0U;
volatile uint32_t foc_isr_max_cycles = 0U;
volatile uint32_t foc_isr_budget_cycles = 0U;
volatile uint32_t foc_isr_overrun_count = 0U;
volatile uint32_t current_telemetry_tx_count = 0U;
volatile uint32_t current_telemetry_drop_count = 0U;
volatile uint32_t app_task_iterations = 0U;
volatile uint32_t app_task_deadline_misses = 0U;
volatile uint32_t app_task_last_cycles = 0U;
volatile uint32_t app_task_max_cycles = 0U;
volatile uint32_t app_stack_min_free_bytes = 0U;
volatile uint32_t app_heap_free_bytes = 0U;
volatile uint32_t app_heap_min_free_bytes = 0U;
volatile uint32_t app_feedback_invalid_count = 0U;
volatile uint32_t app_adc_missed_count = 0U;
volatile uint32_t app_start_rejected_count = 0U;
volatile uint32_t app_critical_max_cycles = 0U;

static volatile uint8_t runtime_initialized;
static volatile uint8_t runtime_started;
static volatile uint8_t power_pwm_prepared;
static uint8_t speed_loop_divider;
static uint32_t feedback_age_limit_cycles;
static uint32_t last_telemetry_tick;
static uint32_t last_diagnostic_tick;
static uint8_t telemetry_buffer[96];
static volatile float telemetry_ref_rpm;
static volatile float telemetry_feedback_rpm;

static void RecordCriticalCycles(uint32_t started)
{
    uint32_t elapsed = (uint32_t)(DWT->CYCCNT - started);
    if (elapsed > app_critical_max_cycles)
    {
        app_critical_max_cycles = elapsed;
    }
}

#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
static void PreparePowerPWM(void)
{
    GPIO_InitTypeDef pin = {0};
    uint32_t saved;
    uint32_t started;
    /* Gate EN is still inactive: only TIM1 can enable it, after prepared=1.
     * HAL startup calls are deliberately outside the PRIMASK section. */
    if ((HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) ||
        (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) ||
        (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) != HAL_OK) ||
        (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3) != HAL_OK))
    {
        BoardSafety_Fatal(BOARD_FAULT_HAL);
    }
    BoardSafety_DisableOutputs();
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Alternate = GPIO_AF1_TIM1;
    pin.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_8 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &pin);

    saved = __get_PRIMASK();
    __disable_irq();
    started = DWT->CYCCNT;
    if (board_fault_mask == 0U)
    {
        power_pwm_prepared = 1U;
    }
    else
    {
        /* Reassert all fault outputs after any interrupted HAL preparation. */
        BoardSafety_LatchFault(board_fault_mask);
    }
    __DMB();
    RecordCriticalCycles(started);
    __set_PRIMASK(saved);
}
#endif

static void SendTelemetry(void)
{
    int length;
    float reference;
    float feedback;
    uint32_t saved;
    uint32_t started;
    if (huart1.gState != HAL_UART_STATE_READY)
    {
        current_telemetry_drop_count++;
        return; /* The IRQ still owns the previous transmission buffer. */
    }
    saved = __get_PRIMASK();
    __disable_irq();
    started = DWT->CYCCNT;
    reference = telemetry_ref_rpm;
    feedback = telemetry_feedback_rpm;
    __DMB();
    RecordCriticalCycles(started);
    __set_PRIMASK(saved);

    length = snprintf((char *)telemetry_buffer, sizeof(telemetry_buffer),
                      "%.3f,%.3f\r\n", (double)reference, (double)feedback);
    if (length <= 0)
    {
        current_telemetry_drop_count++;
        return;
    }
    if (length >= (int)sizeof(telemetry_buffer))
    {
        length = (int)sizeof(telemetry_buffer) - 1;
    }
    if (HAL_UART_Transmit_IT(&huart1, telemetry_buffer, (uint16_t)length) == HAL_OK)
    {
        current_telemetry_tx_count++;
    }
    else
    {
        current_telemetry_drop_count++;
    }
}

void AppRuntime_Init(void)
{
    /* MX_TIM1_Init configured AF pins; restore GPIO-low until calibration. */
    BoardSafety_InitSafe();
    CurrentSense_Init();
    CurrentMonitor_Init();
    AD2S1210_Init();
    if ((AD2S1210_IsInitialized() == 0U) || (ad2s_dwt_ready == 0U))
    {
        BoardSafety_Fatal(BOARD_FAULT_SENSOR_INIT);
    }
    OpenFOC_Init();
    OpenFOC_SetUqVoltageNorm(CURRENT_LOOP_UQ_FF_NORM);
    foc_uq_open_loop_norm = RESOLVER_TEST_UQ_NORM;
    AxisCommandPort_Init();
    OpenFOC_ApplyCommand(NULL);
    /* Actual center-aligned period = 2*ARR*(PSC+1) CPU clocks (both 200 MHz). */
    foc_isr_budget_cycles = 2U * htim1.Init.Period * (htim1.Init.Prescaler + 1U);
    feedback_age_limit_cycles = (SystemCoreClock / 1000U) * APP_FEEDBACK_MAX_AGE_MS;
    runtime_initialized = 1U;
}

void AppRuntime_TaskStart(void)
{
    if ((runtime_initialized == 0U) || (osKernelGetState() != osKernelRunning) ||
        (osKernelGetTickFreq() != 1000U) || (board_fault_mask != 0U))
    {
        BoardSafety_Fatal(BOARD_FAULT_KERNEL);
    }
    last_telemetry_tick = HAL_GetTick();
    last_diagnostic_tick = last_telemetry_tick;
    app_heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
    app_heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    CurrentSense_Start();
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK)
    {
        BoardSafety_Fatal(BOARD_FAULT_HAL);
    }
    /* OC4REF/TRGO2 is upstream of MOE; keep CH4 and the counter running. */
    BoardSafety_DisableOutputs();
    runtime_started = 1U;
    __DMB();
    if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
    {
        BoardSafety_Fatal(BOARD_FAULT_HAL);
    }
}

void AppRuntime_BackgroundStep(void)
{
    uint32_t now;
    AD2S1210_Run_1kHz();
#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
    if ((power_pwm_prepared == 0U) && (CurrentMonitor_IsReady() != 0U) &&
        (board_fault_mask == 0U))
    {
        PreparePowerPWM();
    }
#endif
    now = HAL_GetTick();
    if ((uint32_t)(now - last_telemetry_tick) >= APP_TELEMETRY_PERIOD_MS)
    {
        last_telemetry_tick = now;
        SendTelemetry();
    }
    if ((uint32_t)(now - last_diagnostic_tick) >= APP_DIAGNOSTIC_PERIOD_MS)
    {
        last_diagnostic_tick = now;
        app_stack_min_free_bytes =
            (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);
        app_heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
        app_heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    }
    app_task_iterations++;
}

void AppRuntime_FastISR(void)
{
    AD2S1210_FeedbackSnapshot snapshot;
    const AxisCommandFrame *command_frame;
    uint8_t command_permit;
    uint32_t started = DWT->CYCCNT;
    uint32_t before;
    uint32_t now_ms;
    uint32_t now_cycles;
    uint32_t saved;
    uint32_t critical_started;
    uint32_t elapsed;
    uint32_t status_inhibits = 0U;
    uint8_t feedback_fresh;
    uint8_t feedback_ok;
    uint8_t sample_ok;
    uint8_t allow;

    if (runtime_started == 0U)
    {
        return;
    }
    if (board_fault_mask != 0U)
    {
        foc_run_request = 0U;
        BoardSafety_DisableOutputs();
    }
    AD2S1210_GetSnapshot(&snapshot);
    now_ms = HAL_GetTick();
    now_cycles = DWT->CYCCNT;
    feedback_fresh = (uint8_t)APP_FEEDBACK_FRESH(now_ms, snapshot.sample_tick_ms,
                           now_cycles, snapshot.sample_cycles, feedback_age_limit_cycles);
    feedback_ok = (uint8_t)((snapshot.valid != 0U) && (snapshot.speed_valid != 0U) &&
                           (feedback_fresh != 0U));
    if (feedback_ok == 0U)
    {
        app_feedback_invalid_count++;
    }

    if ((board_output_enabled != 0U) && (feedback_ok == 0U))
    {
        BoardSafety_LatchFault(BOARD_FAULT_FEEDBACK);
        foc_run_request = 0U;
    }
    /* Whole command handoff precedes every slow/fast control calculation.
     * START before readiness is rejected now, never deferred into auto-start. */
    command_frame = AxisCommandPort_BeginCycle(now_ms,
        (uint32_t)APP_CAN_ENABLE(1U, power_pwm_prepared,
            CurrentMonitor_IsReady(), feedback_ok, board_fault_mask,
            resolver_dynamic_test_lockout_active));
    command_permit = (uint8_t)(command_frame != NULL);
    OpenFOC_ApplyCommand(command_frame);
    if (board_fault_mask == 0U)
    {
        OpenFOC_SetRotorFeedback(snapshot.position_rad, snapshot.speed_rpm, feedback_ok);
        speed_loop_divider++;
        if (speed_loop_divider >= 20U)
        {
            speed_loop_divider = 0U;
            OpenFOC_Ramp_1ms();
        }
    }

    before = monitor_sample_sequence;
    CurrentMonitor_Run_20kHz(foc_theta);
    sample_ok = (uint8_t)(monitor_sample_sequence != before);
    if (sample_ok == 0U)
    {
        app_adc_missed_count++;
    }
    if ((board_output_enabled != 0U) || (command_permit != 0U))
    {
        uint32_t reason = 0U;
        if (feedback_ok == 0U) reason |= BOARD_FAULT_FEEDBACK;
        if (sample_ok == 0U) reason |= BOARD_FAULT_ADC_FRAME;
        if (reason != 0U)
        {
            BoardSafety_LatchFault(reason);
            AxisCommandPort_RejectActive(AXIS_CMD_FAULT);
            command_permit = 0U;
            OpenFOC_ApplyCommand(NULL);
        }
    }

    if (board_fault_mask == 0U)
    {
        /* All control-state writes stay in this ISR after startup. */
        OpenFOC_SetCurrentFeedback(monitor_id_a, monitor_iq_a,
            (uint8_t)((CurrentMonitor_IsReady() != 0U) && (sample_ok != 0U)));
        OpenFOC_EnforceCommandLimit();
        OpenFOC_Run_20kHz();
    }

    /* Observe existing prerequisites. This diagnostic has no control authority. */
    if (axis_command_configured == 0U) status_inhibits |= AXIS_INHIBIT_COMMAND_CONFIG;
    if (command_permit == 0U) status_inhibits |= AXIS_INHIBIT_NO_COMMAND;
    if (power_pwm_prepared == 0U) status_inhibits |= AXIS_INHIBIT_PWM_NOT_PREPARED;
    if (CurrentMonitor_IsReady() == 0U) status_inhibits |= AXIS_INHIBIT_CURRENT_OFFSETS;
    if (snapshot.valid == 0U) status_inhibits |= AXIS_INHIBIT_POSITION;
    if (snapshot.speed_valid == 0U) status_inhibits |= AXIS_INHIBIT_SPEED;
    if (feedback_fresh == 0U) status_inhibits |= AXIS_INHIBIT_FEEDBACK_AGE;
    if (sample_ok == 0U) status_inhibits |= AXIS_INHIBIT_ADC_FRAME;
    if (resolver_dynamic_test_lockout_active != 0U) status_inhibits |= AXIS_INHIBIT_TEST_LOCKOUT;
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_DISABLED)
    status_inhibits |= AXIS_INHIBIT_POWER_DISABLED;
#endif
    saved = __get_PRIMASK();
    __disable_irq();
    critical_started = DWT->CYCCNT;
    command_permit = (uint8_t)AxisCommandPort_FinalPermit(HAL_GetTick());
    if (command_permit == 0U)
    {
        status_inhibits |= AXIS_INHIBIT_NO_COMMAND;
    }
    allow = (uint8_t)APP_CAN_ENABLE(command_permit, power_pwm_prepared,
        CurrentMonitor_IsReady(), (uint8_t)(feedback_ok && sample_ok),
        board_fault_mask, resolver_dynamic_test_lockout_active);
    BoardSafety_SetOutputsEnabled(allow);
    if (board_fault_mask != 0U) status_inhibits |= AXIS_INHIBIT_FAULT;
    AxisStatus_Publish(board_output_enabled, foc_aligning, status_inhibits);
    telemetry_ref_rpm = foc_speed_ref_rpm;
    telemetry_feedback_rpm = foc_speed_feedback_rpm;
    __DMB();
    RecordCriticalCycles(critical_started);
    __set_PRIMASK(saved);
    /* Any final expiry/fault already removed hardware permission. Reset PI
     * outside PRIMASK; TIM1 still owns these private control variables. */
    if (command_permit == 0U) OpenFOC_ApplyCommand(NULL);

    elapsed = (uint32_t)(DWT->CYCCNT - started);
    foc_isr_last_cycles = elapsed;
    if (elapsed > foc_isr_max_cycles) foc_isr_max_cycles = elapsed;
    if (elapsed > foc_isr_budget_cycles) foc_isr_overrun_count++;
}
