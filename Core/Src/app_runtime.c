#include "main.h"
#include "app_runtime.h"
#include "app_policy.h"
#include "axis_status.h"
#include "axis_command.h"
#include "board_config.h"
#if AXIS_COMMAND_BENCH_ENABLE
#include "axis_command_bench.h"
#include "can_link.h"
#endif
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
#include "telemetry_format.h"

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
#if AXIS_COMMAND_BENCH_ENABLE
/* Bench-only diagnostic switch: 1 enables the low-priority idle scan.
 * Never changes output permission, feedback validity or kernel protection. */
volatile uint32_t app_stack_probe_enabled = 1U;
#endif
/* Written only by the idle hook; UINT32_MAX means NOT MEASURED.
 * Count/time are diagnostic freshness, not a coherent control snapshot. */
volatile uint32_t app_stack_min_free_bytes = UINT32_MAX;
volatile uint32_t app_stack_probe_count = 0U;
volatile uint32_t app_stack_probe_last_tick_ms = 0U;
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
#if AXIS_COMMAND_BENCH_ENABLE
static volatile uint32_t command_bench_ready; /* atomic scalar TIM1 -> appTask */
volatile AppGapTrace app_gap_trace = {
    0U, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
typedef struct {
    uint32_t acquisition_cycles;
    uint32_t telemetry_cycles;
    uint32_t diagnostic_cycles;
} AppWorkTiming;
static AppWorkTiming previous_work_timing;
static uint32_t previous_body_end_cycles;
static uint32_t previous_body_misses;
static uint32_t previous_work_ready;

static void CaptureSampleGap(uint32_t entry_cycles, uint32_t gap_before)
{
    AD2S1210_FeedbackSnapshot feedback;
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    if ((app_gap_trace.captured != 0U) || (previous_work_ready == 0U) ||
        (ad2s_sample_gap_count == gap_before) || (cycles_per_us == 0U)) return;
    /* The task is the sole acquisition writer. This snapshot is the just-read
     * sample; TIM1 only consumes it. Capture AFTER a real driver gap, once. */
    AD2S1210_GetSnapshot(&feedback);
    app_gap_trace.previous_total_us = app_task_last_cycles / cycles_per_us;
    app_gap_trace.previous_acquisition_us =
        previous_work_timing.acquisition_cycles / cycles_per_us;
    app_gap_trace.previous_telemetry_us =
        previous_work_timing.telemetry_cycles / cycles_per_us;
    app_gap_trace.previous_diagnostics_us =
        previous_work_timing.diagnostic_cycles / cycles_per_us;
    app_gap_trace.sample_interval_us = ad2s_sample_interval_cycles / cycles_per_us;
    app_gap_trace.between_tasks_us =
        (uint32_t)(entry_cycles - previous_body_end_cycles) / cycles_per_us;
    app_gap_trace.current_entry_to_sample_us =
        (uint32_t)(feedback.sample_cycles - entry_cycles) / cycles_per_us;
    app_gap_trace.misses_since_previous_body =
        (uint32_t)(app_task_deadline_misses - previous_body_misses);
    app_gap_trace.previous_iteration = app_task_iterations;
    app_gap_trace.gap_count = ad2s_sample_gap_count;
    /* Compatibility field: stack scanning no longer runs in appTask. */
    app_gap_trace.previous_stack_scan_ran = 0U;
    app_gap_trace.dwt_ready = ad2s_dwt_ready;
    /* Debugger observes the completed record; do not rewrite after publication.
     * No ISR consumes this record, so no additional IRQ masking is needed. */
    __DMB();
    app_gap_trace.captured = 1U;
}
#endif
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
    size_t length;
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

    /* Fixed-format integer conversion keeps this sampling task bounded.
     * Invalid/out-of-encoding-range data drops the whole telemetry frame;
     * it never alters the controller values or relaxes their validation. */
    length = TelemetryFormat_SpeedCsv((char *)telemetry_buffer,
                                     sizeof(telemetry_buffer), reference, feedback);
    if (length == 0U)
    {
        current_telemetry_drop_count++;
        return;
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
#if AXIS_COMMAND_BENCH_ENABLE
    CanLink_Init(); /* startup only, before periodic acquisition begins */
#endif
    CurrentSense_Start();
#if AXIS_COMMAND_BENCH_ENABLE
    AxisCommandBench_Init(HAL_GetTick());
    /* TIM1 CH4 is an internal ADC trigger. Directly enable that channel;
     * HAL_TIM_PWM_Start would transiently set the advanced-timer MOE. */
    BoardSafety_DisableOutputs();
    SET_BIT(TIM1->CCER, TIM_CCER_CC4E);
#else
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK)
    {
        BoardSafety_Fatal(BOARD_FAULT_HAL);
    }
#endif
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
#if AXIS_COMMAND_BENCH_ENABLE
    uint32_t entry_cycles = DWT->CYCCNT;
    uint32_t gap_before = ad2s_sample_gap_count;
    uint32_t section_started;
    AppWorkTiming work_timing;
    work_timing.telemetry_cycles = 0U;
    work_timing.diagnostic_cycles = 0U;
#endif
    AD2S1210_Run_1kHz();
#if AXIS_COMMAND_BENCH_ENABLE
    work_timing.acquisition_cycles = (uint32_t)(DWT->CYCCNT - entry_cycles);
    CaptureSampleGap(entry_cycles, gap_before);
    CanLink_Poll(); /* bounded link service, after resolver sampling */
#endif
#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
    if ((power_pwm_prepared == 0U) && (CurrentMonitor_IsReady() != 0U) &&
        (board_fault_mask == 0U))
    {
        PreparePowerPWM();
    }
#endif
    now = HAL_GetTick();
#if AXIS_COMMAND_BENCH_ENABLE
    AxisCommandBench_Step(now, command_bench_ready,
        BoardSafety_PowerOutputsAreDisabled(), board_fault_mask, foc_isr_overrun_count);
#endif
    if ((uint32_t)(now - last_telemetry_tick) >= APP_TELEMETRY_PERIOD_MS)
    {
        last_telemetry_tick = now;
#if AXIS_COMMAND_BENCH_ENABLE
        section_started = DWT->CYCCNT;
#endif
        SendTelemetry();
#if AXIS_COMMAND_BENCH_ENABLE
        work_timing.telemetry_cycles = (uint32_t)(DWT->CYCCNT - section_started);
#endif
    }
    if ((uint32_t)(now - last_diagnostic_tick) >= APP_DIAGNOSTIC_PERIOD_MS)
    {
        last_diagnostic_tick = now;
#if AXIS_COMMAND_BENCH_ENABLE
        section_started = DWT->CYCCNT;
#endif
        /* Full stack scanning belongs to the preemptible idle hook. */
        app_heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
        app_heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
#if AXIS_COMMAND_BENCH_ENABLE
        work_timing.diagnostic_cycles = (uint32_t)(DWT->CYCCNT - section_started);
#endif
    }
    app_task_iterations++;
#if AXIS_COMMAND_BENCH_ENABLE
    previous_work_timing = work_timing;
    previous_body_misses = app_task_deadline_misses;
    previous_work_ready = (uint32_t)(CurrentMonitor_IsReady() != 0U);
    previous_body_end_cycles = DWT->CYCCNT;
#endif
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
#if AXIS_COMMAND_BENCH_ENABLE
    /* Read before any routine disable can conceal an unexpected output bit. */
    if (BoardSafety_PowerOutputsAreDisabled() == 0U)
        BoardSafety_LatchFault(BOARD_FAULT_OUTPUT_GUARD);
#endif
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
#if AXIS_COMMAND_BENCH_ENABLE
    /* Software command readiness is separate from output permission. Power
     * remains compiled out; no test frame is installed in the FOC controller. */
    command_bench_ready = (uint32_t)APP_CAN_ENABLE(1U, 1U,
        CurrentMonitor_IsReady(), feedback_ok, board_fault_mask,
        resolver_dynamic_test_lockout_active);
    command_frame = AxisCommandPort_BeginCycle(now_ms, command_bench_ready);
    command_permit = (uint8_t)(command_frame != NULL);
    OpenFOC_ApplyCommand(NULL);
#else
    command_frame = AxisCommandPort_BeginCycle(now_ms,
        (uint32_t)APP_CAN_ENABLE(1U, power_pwm_prepared,
            CurrentMonitor_IsReady(), feedback_ok, board_fault_mask,
            resolver_dynamic_test_lockout_active));
    command_permit = (uint8_t)(command_frame != NULL);
    OpenFOC_ApplyCommand(command_frame);
#endif
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
    AxisStatus_Publish(board_output_enabled,
        (uint32_t)(OpenFOC_GetRunPhase() == FOC_PHASE_ALIGNING), status_inhibits);
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
