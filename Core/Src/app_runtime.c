#include "main.h"
#include "app_runtime.h"
#include "rng.h"
#include "app_policy.h"
#include "axis_status.h"
#include "axis_command.h"
#include "board_config.h"
#if AXIS_COMMAND_BENCH_ENABLE
#include "can_link.h"
#include "can_link_protocol.h"
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

/* BOOT IDENTITY BEGIN: tested from this production block on the host. */
static uint32_t boot_identity_attempted;
static uint32_t boot_identity_state = APP_BOOT_STATE_NOT_READY;
static uint32_t boot_identity_words[4];
volatile uint32_t app_boot_identity_state = APP_BOOT_STATE_NOT_READY;
volatile uint32_t app_boot_error_flags;

/* Only the startup context reads RNG. No other reader/IRQ may consume DR.
 * HAL H750 lacks the CONDRST-path seed checks, so inspect current AND sticky
 * error bits before/after each word; never clear them or substitute entropy. */
static uint32_t BootRngStatusErrors(void)
{
    uint32_t sr = hrng.Instance->SR;
    uint32_t errors = 0U;
    if ((sr & (RNG_SR_CECS | RNG_SR_CEIS)) != 0U)
        errors |= APP_BOOT_ERROR_CLOCK;
    if ((sr & (RNG_SR_SECS | RNG_SR_SEIS)) != 0U)
        errors |= APP_BOOT_ERROR_SEED;
    return errors;
}

static uint32_t ReadBootWord(uint32_t *word)
{
    uint32_t errors = BootRngStatusErrors();
    HAL_StatusTypeDef result;
    if (errors != 0U)
    {
        app_boot_error_flags |= errors;
        return 0U;
    }
    /* HAL timeout uses the live TIM7 tick (>2 ms), NOT a hard independent
     * watchdog. Caller is pre-scheduler with interrupts enabled; no retries. */
    result = HAL_RNG_GenerateRandomNumber(&hrng, word);
    errors = BootRngStatusErrors();
    if ((hrng.ErrorCode & HAL_RNG_ERROR_TIMEOUT) != 0U)
        errors |= APP_BOOT_ERROR_TIMEOUT;
    if ((result != HAL_OK) || (hrng.State != HAL_RNG_STATE_READY) ||
        (hrng.ErrorCode != HAL_RNG_ERROR_NONE))
        errors |= APP_BOOT_ERROR_HAL;
    /* RM0433 34.3.5 recommends rejecting zero after DR reads as well,
     * covering a seed/DRDY race even when status sampling missed it. */
    if ((errors == 0U) && (*word == 0U)) errors |= APP_BOOT_ERROR_ZERO;
    app_boot_error_flags |= errors;
    return (errors == 0U) ? 1U : 0U;
}

static void InitBootIdentity(void)
{
    uint32_t candidate[4];
    uint32_t previous;
    unsigned int i;
    if (boot_identity_attempted != 0U) return;
    boot_identity_attempted = 1U;
    /* Failure is sticky for this boot. Private words remain zero until the
     * whole candidate is validated. No control permission is touched. */
    if ((hrng.Instance != RNG) || (hrng.State != HAL_RNG_STATE_READY) ||
        (hrng.ErrorCode != HAL_RNG_ERROR_NONE) ||
        (hrng.Init.ClockErrorDetection != RNG_CED_ENABLE) ||
        (__get_PRIMASK() != 0U) || (__get_IPSR() != 0U))
    {
        app_boot_error_flags |= APP_BOOT_ERROR_CONFIG;
        goto failed;
    }
    if (((hrng.Instance->CR & RNG_CR_RNGEN) == 0U) ||
        ((hrng.Instance->CR & (RNG_CR_CED | RNG_CR_IE)) != 0U))
    {
        app_boot_error_flags |= APP_BOOT_ERROR_CONFIG;
        goto failed;
    }
    /* First word is the continuous-comparison reference, not identity data.
     * Every subsequent word must differ from its immediate predecessor.
     * This conservative check is NOT a statistical randomness certification. */
    if (ReadBootWord(&previous) == 0U) goto failed;
    for (i = 0U; i < 4U; ++i)
    {
        if (ReadBootWord(&candidate[i]) == 0U) goto failed;
        if (candidate[i] == previous)
        {
            app_boot_error_flags |= APP_BOOT_ERROR_REPEAT;
            goto failed;
        }
        previous = candidate[i];
    }
    for (i = 0U; i < 4U; ++i) boot_identity_words[i] = candidate[i];
    /* No task/TIM1 consumer exists yet. Publication is complete before the
     * scheduler starts; state/words are never rewritten until MCU reset. */
    boot_identity_state = APP_BOOT_STATE_VALID;
    app_boot_identity_state = boot_identity_state;
    return;
failed:
    boot_identity_state = APP_BOOT_STATE_FAILED;
    app_boot_identity_state = boot_identity_state;
}

uint32_t AppRuntime_GetBootIdentity(uint32_t words[4])
{
    unsigned int i;
    if (words != NULL)
    {
        for (i = 0U; i < 4U; ++i)
            words[i] = (boot_identity_state == APP_BOOT_STATE_VALID) ?
                       boot_identity_words[i] : 0U;
    }
    return boot_identity_state;
}
/* BOOT IDENTITY END */

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
static uint32_t previous_body_end_cycles;
static uint32_t previous_body_misses;
static uint32_t previous_work_ready;

/* TASK INTERVAL TRACE BEGIN */
typedef struct {
    uint32_t acquisition_cycles;
    uint32_t acquisition_end_cycles;
    uint32_t telemetry_cycles;
    uint32_t diagnostic_cycles;
} AppWorkTiming;
static AppWorkTiming previous_work_timing;

volatile AppTaskIntervalTrace app_task_interval_trace = {
    APP_TASK_INTERVAL_WAITING, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX
};

static void CaptureTaskInterval(uint32_t previous_ready, uint32_t previous_end,
                                uint32_t entry)
{
    AD2S1210_FeedbackSnapshot next;
    uint32_t state = APP_TASK_INTERVAL_UNAVAILABLE;
    uint32_t span = ad2s_age_trace.edge_interval_cycles;
    uint32_t source_edge;
    uint32_t end_offset;
    uint32_t entry_offset;
    uint32_t acquisition_offset;

    /* The single scheduled publisher has just returned. A calibrated previous
     * step supplies its end marker, including a legitimate timer value of 0. */
    if ((previous_ready == 0U) ||
        ((ad2s_age_trace.state != AD2S_AGE_EDGE_BY_CHECK) &&
         (ad2s_age_trace.state != AD2S_AGE_EDGE_AFTER_CHECK)) ||
        (span == 0U) || (span >= 0x80000000UL))
        goto publish;

    AD2S1210_GetSnapshot(&next);
    if ((next.valid == 0U) || (next.sequence != ad2s_age_trace.next_sequence) ||
        (next.sequence != (uint32_t)(ad2s_age_trace.source_sequence + 1U)))
        goto publish;

    source_edge = (uint32_t)(next.sample_cycles - span);
    end_offset = (uint32_t)(previous_end - source_edge);
    entry_offset = (uint32_t)(entry - source_edge);
    if ((end_offset > entry_offset) || (entry_offset > span))
        goto publish;

    app_task_interval_trace.old_edge_to_end_cycles = end_offset;
    app_task_interval_trace.between_steps_cycles = entry_offset - end_offset;
    app_task_interval_trace.entry_to_edge_cycles = span - entry_offset;

    /* Both details belong to the source step. The driver marker follows
     * publication; the telemetry measurement only covers the later queue attempt. */
    acquisition_offset = (uint32_t)(previous_work_timing.acquisition_end_cycles - source_edge);
    if ((acquisition_offset <= end_offset) &&
        (previous_work_timing.telemetry_cycles <= end_offset - acquisition_offset))
    {
        app_task_interval_trace.old_edge_to_acquisition_end_cycles = acquisition_offset;
        app_task_interval_trace.old_telemetry_queue_cycles = previous_work_timing.telemetry_cycles;
    }
    state = APP_TASK_INTERVAL_COMPLETE;
publish:
    __DMB();
    app_task_interval_trace.state = state;
}
/* TASK INTERVAL TRACE END */

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

#if AXIS_COMMAND_BENCH_ENABLE
volatile AppReadyLossTrace app_ready_loss_trace;
static volatile uint32_t app_task_phase;
/* FastISR body-start history, not hardware IRQ-entry or total IRQ duration. */
static uint32_t previous_fast_body_entry;
static uint32_t previous_fast_body_valid;

/* READY LOSS TRACE BEGIN: tested from this production block on the host. */
static void CaptureReadyLoss(uint32_t previous_permit, uint32_t epoch_before,
                             uint32_t ready, uint32_t now_cycles, uint32_t now_ms,
                             const AD2S1210_FeedbackSnapshot *snapshot,
                             uint32_t current_ready, uint32_t lockout, uint32_t fault)
{
    uint32_t reasons = 0U;
    uint32_t age_cycles;
    uint32_t age_ms;
    if ((app_ready_loss_trace.captured != 0U) || (previous_permit == 0U) ||
        (ready != 0U) || (axis_command_result != AXIS_CMD_NOT_READY) ||
        (axis_command_epoch == epoch_before)) return;
    age_cycles = (uint32_t)(now_cycles - snapshot->sample_cycles);
    age_ms = (uint32_t)(now_ms - snapshot->sample_tick_ms);
    if (snapshot->valid == 0U) reasons |= APP_READY_LOSS_POSITION;
    if (snapshot->speed_valid == 0U) reasons |= APP_READY_LOSS_SPEED;
    if (age_cycles > feedback_age_limit_cycles) reasons |= APP_READY_LOSS_DWT_AGE;
    if (age_ms > APP_FEEDBACK_MAX_AGE_MS) reasons |= APP_READY_LOSS_HAL_AGE;
    if (current_ready == 0U) reasons |= APP_READY_LOSS_CURRENT;
    if (lockout != 0U) reasons |= APP_READY_LOSS_LOCKOUT;
    if (fault != 0U) reasons |= APP_READY_LOSS_FAULT;
    app_ready_loss_trace.reason_bits = reasons;
    app_ready_loss_trace.age_cycles = age_cycles;
    app_ready_loss_trace.age_ms = age_ms;
    app_ready_loss_trace.sample_seq = snapshot->sequence;
    app_ready_loss_trace.command_seq = axis_command_result_seq;
    app_ready_loss_trace.epoch_before = epoch_before;
    app_ready_loss_trace.epoch_after = axis_command_epoch;
    app_ready_loss_trace.task_phase = app_task_phase;
    app_ready_loss_trace.gap_count = ad2s_sample_gap_count;
    app_ready_loss_trace.spi_errors = ad2s_spi_error_count;
    app_ready_loss_trace.deadline_misses = app_task_deadline_misses;
    /* Pair the next resolver publication with the snapshot and clock value
     * used by this TIM1 check. appTask can resume after this IRQ returns. */
    if ((reasons & APP_READY_LOSS_DWT_AGE) != 0U)
        AD2S1210_ArmAgeTrace(snapshot, now_cycles);
    __DMB();
    app_ready_loss_trace.captured = 1U;
}
/* READY LOSS TRACE END */

/* appTask reads diagnostic mirrors on STATUS/FAULT and STOP result rereads.
 * PRIMASK excludes TIM1 IRQ0 while copying. Mirrors can reflect different
 * publication times (e.g. fault before next permit update); never authorize
 * control from this snapshot. Fatal/NMI paths disable outputs and do not return. */
static void ReadCanStatus(CanLinkReadStatus *snapshot)
{
    uint32_t saved = __get_PRIMASK();
    uint32_t started;
    __disable_irq();
    started = DWT->CYCCNT;
    snapshot->status_word = axis_status_word;
    snapshot->fault_mask = board_fault_mask;
    snapshot->configured = axis_command_configured;
    snapshot->permit = axis_command_permit;
    snapshot->output_enabled = board_output_enabled;
    /* PublishDiagnostics writes this only after the core consumes STOP and
     * BeginCycle has disabled outputs; a stale read delays, never fakes, ACK. */
    snapshot->stop_applied_ticket = axis_command_stop_applied;
    snapshot->run_epoch = axis_command_epoch;
    __DMB();
    RecordCriticalCycles(started);
    __set_PRIMASK(saved);
}

/* CAN is the sole Bench command source. The port owns the IRQ-safe STOP
 * handoff; TIM1 owns disarm/consumption. No output or fault writes here. */
static uint32_t RequestCanStop(uint32_t *ticket)
{
    return (AxisCommandPort_Stop(ticket) == AXIS_CMD_OK) ? 1U : 0U;
}

/* Integer wire units -> common command limits, with no runtime publication.
 * Passing this precheck does not establish readiness, freshness or permission. */
static uint8_t CheckCanTargets(int32_t speed_rpm, uint32_t iq_limit_ma)
{
    uint32_t result = AxisCommandPort_CheckTargets((float)speed_rpm,
                                                  (float)iq_limit_ma / 1000.0f);
    if (result == AXIS_CMD_OK) return CAN_TARGET_CHECK_OK;
    if (result == AXIS_CMD_INVALID) return CAN_TARGET_CHECK_RANGE;
    if (result == AXIS_CMD_CONFIG_UNSET) return CAN_TARGET_CHECK_CONFIG;
    return CAN_TARGET_CHECK_REJECTED;
}

/* CAN owns the immutable complete request; the port binds original SOF time
 * and core sequence atomically. Bench commands never reach the FOC controller. */
static uint32_t SubmitCanCommand(const CanLinkCommandRequest *request, uint32_t *seq)
{
    AxisCommandFrame frame = {0};
    if (seq != NULL) *seq = 0U;
    if ((request == NULL) || (seq == NULL) || (request->ttl_ms == 0U) ||
        (request->ttl_ms > 1000U))
        return AXIS_CMD_INVALID;
    frame.run_epoch = request->run_epoch;
    frame.opcode = request->opcode;
    frame.control_mode = AXIS_CMD_SPEED_MODE;
    frame.speed_target_rpm = (float)request->speed_rpm;
    frame.iq_limit_a = (float)request->iq_limit_ma / 1000.0f;
    return AxisCommandPort_SubmitTimed(&frame, request->begin_timestamp,
                                      request->ttl_ms * 1000U / CAN_TIME_TICK_US, seq);
}

static uint32_t ReadCanReceipt(CanLinkCommandReceipt *receipt)
{
    AxisCommandReceipt local;
    if ((receipt == NULL) || (AxisCommandPort_GetReceipt(&local) != AXIS_CMD_OK))
        return 0U;
    receipt->command_seq = local.command_seq;
    receipt->result = local.result;
    receipt->control_seq = local.control_seq;
    return 1U;
}

static const CanLinkProviders can_providers = {
    ReadCanStatus, RequestCanStop, CheckCanTargets, AppRuntime_GetBootIdentity,
    SubmitCanCommand, ReadCanReceipt
};
#endif

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

#if AXIS_COMMAND_BENCH_ENABLE
/* BENCH TELEMETRY BEGIN */
/* Single Cortex-M core: appTask is the sole producer and runs above Idle.
 * Keep the slot occupied even while Idle is preempted in the formatter/HAL. */
enum { TELEMETRY_SLOT_FREE, TELEMETRY_SLOT_QUEUED, TELEMETRY_SLOT_EXPIRED };
static volatile uint32_t telemetry_pending;
static volatile uint32_t telemetry_pending_tick;
static volatile float telemetry_pending_reference;
static volatile float telemetry_pending_feedback;

static void QueueTelemetry(uint32_t now)
{
    uint32_t saved;
    uint32_t started;
    if (telemetry_pending != TELEMETRY_SLOT_FREE)
    {
        /* The next due tick ends the old slot's usefulness. Remember this
         * even if Idle starvation spans a complete HAL tick wrap. */
        telemetry_pending = TELEMETRY_SLOT_EXPIRED;
        current_telemetry_drop_count++;
        return;
    }
    if (huart1.gState != HAL_UART_STATE_READY)
    {
        current_telemetry_drop_count++;
        return;
    }
    saved = __get_PRIMASK();
    __disable_irq();
    started = DWT->CYCCNT;
    telemetry_pending_reference = telemetry_ref_rpm;
    telemetry_pending_feedback = telemetry_feedback_rpm;
    telemetry_pending_tick = now;
    __DMB();
    telemetry_pending = TELEMETRY_SLOT_QUEUED;
    RecordCriticalCycles(started);
    __set_PRIMASK(saved);
}

/* Idle is the only UART submitter. The transmit IRQ owns telemetry_buffer
 * until gState returns to READY, independently of the pending sample slot. */
void AppRuntime_ServiceIdleTelemetry(void)
{
    size_t length = 0U;
    if (telemetry_pending == TELEMETRY_SLOT_FREE) return;
    __DMB();
    if ((telemetry_pending == TELEMETRY_SLOT_QUEUED) &&
        (huart1.gState == HAL_UART_STATE_READY))
    {
        length = TelemetryFormat_SpeedCsv((char *)telemetry_buffer,
                     sizeof(telemetry_buffer), telemetry_pending_reference,
                     telemetry_pending_feedback);
    }
    /* Recheck after formatting: Idle can be preempted for arbitrary time.
     * This is a submission-time check; the UART start remains preemptible. */
    if ((length != 0U) &&
        ((uint32_t)(HAL_GetTick() - telemetry_pending_tick) < APP_TELEMETRY_PERIOD_MS) &&
        (telemetry_pending == TELEMETRY_SLOT_QUEUED) &&
        (HAL_UART_Transmit_IT(&huart1, telemetry_buffer, (uint16_t)length) == HAL_OK))
    {
        current_telemetry_tx_count++;
    }
    else
    {
        /* appTask can increment this same counter for a later due sample. */
        uint32_t saved = __get_PRIMASK();
        __disable_irq();
        current_telemetry_drop_count++;
        __set_PRIMASK(saved);
    }
    __DMB();
    telemetry_pending = TELEMETRY_SLOT_FREE;
}
/* BENCH TELEMETRY END */
#else
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

#endif

void AppRuntime_Init(void)
{
    /* MX_TIM1_Init configured AF pins; restore GPIO-low until calibration. */
    BoardSafety_InitSafe();
    InitBootIdentity(); /* MX_RNG_Init has completed; TIM7 tick is live. */
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
    CanLink_Init(&can_providers);
    /* CAN alone submits Bench commands; precheck/identity never publish. */
#endif
    CurrentSense_Start();
#if AXIS_COMMAND_BENCH_ENABLE
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
    app_task_phase = 1U;
#endif
    AD2S1210_Run_1kHz();
#if AXIS_COMMAND_BENCH_ENABLE
    work_timing.acquisition_end_cycles = DWT->CYCCNT;
    work_timing.acquisition_cycles = (uint32_t)(work_timing.acquisition_end_cycles - entry_cycles);
    /* TASK INTERVAL CHECK BEGIN */
    if ((ad2s_age_trace.state >= AD2S_AGE_EDGE_BY_CHECK) &&
        (app_task_interval_trace.state == APP_TASK_INTERVAL_WAITING))
        CaptureTaskInterval(previous_work_ready, previous_body_end_cycles, entry_cycles);
    /* TASK INTERVAL CHECK END */
    CaptureSampleGap(entry_cycles, gap_before);
    /* Task-side sticky expiry observation prevents a stopped TIM1 consumer
     * from seeing a wrapped raw timestamp as a new lease. No output writes. */
    app_task_phase = 2U;
    AxisCommandPort_ObserveTime();
    CanLink_Poll(); /* bounded link service, after resolver sampling */
    app_task_phase = 0U;
#endif
#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
    if ((power_pwm_prepared == 0U) && (CurrentMonitor_IsReady() != 0U) &&
        (board_fault_mask == 0U))
    {
        PreparePowerPWM();
    }
#endif
    now = HAL_GetTick();
    /* Automatic Bench sequencer is intentionally not scheduled alongside CAN.
     * Its source remains available to the independent HOST regression suite. */
    if ((uint32_t)(now - last_telemetry_tick) >= APP_TELEMETRY_PERIOD_MS)
    {
        last_telemetry_tick = now;
#if AXIS_COMMAND_BENCH_ENABLE
        app_task_phase = 3U;
        section_started = DWT->CYCCNT;
        QueueTelemetry(now);
        work_timing.telemetry_cycles = (uint32_t)(DWT->CYCCNT - section_started);
        app_task_phase = 0U;
#else
        SendTelemetry();
#endif
    }
    if ((uint32_t)(now - last_diagnostic_tick) >= APP_DIAGNOSTIC_PERIOD_MS)
    {
        last_diagnostic_tick = now;
#if AXIS_COMMAND_BENCH_ENABLE
        app_task_phase = 4U;
        section_started = DWT->CYCCNT;
#endif
        /* Full stack scanning belongs to the preemptible idle hook. */
        app_heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
        app_heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
#if AXIS_COMMAND_BENCH_ENABLE
        work_timing.diagnostic_cycles = (uint32_t)(DWT->CYCCNT - section_started);
        app_task_phase = 0U;
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
#if AXIS_COMMAND_BENCH_ENABLE
    uint32_t previous_permit;
    uint32_t epoch_before;
    uint32_t ready_current;
    uint32_t ready_lockout;
    uint32_t ready_fault;
    uint16_t raw_u;
    uint16_t raw_v;
    uint16_t raw_w;
    uint16_t raw_bus;
    uint8_t raw_valid;
#endif

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
#if AXIS_COMMAND_BENCH_ENABLE
    /* ADC EARLY READ BEGIN: tested from this production block on the host. */
    /* Only this ISR iteration owns these codes. A miss is never retried later. */
    raw_valid = CurrentSense_HasNewSample();
    if (raw_valid != 0U)
    {
        CurrentSense_GetRaw(&raw_u, &raw_v, &raw_w, &raw_bus);
    }
    /* ADC EARLY READ END */
#endif
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
    previous_permit = axis_command_permit;
    epoch_before = axis_command_epoch;
    ready_current = CurrentMonitor_IsReady();
    ready_lockout = resolver_dynamic_test_lockout_active;
    ready_fault = board_fault_mask;
    command_bench_ready = (uint32_t)APP_CAN_ENABLE(1U, 1U,
        ready_current, feedback_ok, ready_fault, ready_lockout);
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

    /* ADC RAW PROCESS BEGIN: tested from this production block on the host. */
    before = monitor_sample_sequence;
#if AXIS_COMMAND_BENCH_ENABLE
    /* Keep calibration and Park here, with the angle updated above. */
    if (raw_valid != 0U)
        CurrentMonitor_ProcessRaw_20kHz(foc_theta, raw_u, raw_v, raw_w, raw_bus);
#else
    CurrentMonitor_Run_20kHz(foc_theta);
#endif
    sample_ok = (uint8_t)(monitor_sample_sequence != before);
    /* ADC RAW PROCESS END */
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
#if AXIS_COMMAND_BENCH_ENABLE
            uint32_t capture_adc_fault = (uint32_t)((board_fault_mask == 0U) &&
                (command_permit != 0U) && (sample_ok == 0U));
#endif
            BoardSafety_LatchFault(reason);
            AxisCommandPort_RejectActive(AXIS_CMD_FAULT);
            command_permit = 0U;
            OpenFOC_ApplyCommand(NULL);
#if AXIS_COMMAND_BENCH_ENABLE
            /* Freeze the driver's actual failed check only AFTER normal
             * fault/disarm. No live ADC flags are reread here. */
            if (capture_adc_fault != 0U)
                CurrentSense_CaptureFault(started, previous_fast_body_entry,
                    foc_isr_last_cycles, previous_fast_body_valid,
                    axis_command_applied_seq);
#endif
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
#if AXIS_COMMAND_BENCH_ENABLE
    /* Cold diagnostic only, after FinalPermit published the revoke and output
     * handling completed. Its bounded copy cost is included in this ISR time. */
    if ((previous_permit != 0U) && (command_bench_ready == 0U))
        CaptureReadyLoss(previous_permit, epoch_before, command_bench_ready,
                         now_cycles, now_ms, &snapshot,
                         ready_current, ready_lockout, ready_fault);
#endif

#if AXIS_COMMAND_BENCH_ENABLE
    previous_fast_body_entry = started;
    previous_fast_body_valid = 1U;
#endif
    elapsed = (uint32_t)(DWT->CYCCNT - started);
    foc_isr_last_cycles = elapsed;
    if (elapsed > foc_isr_max_cycles) foc_isr_max_cycles = elapsed;
    if (elapsed > foc_isr_budget_cycles) foc_isr_overrun_count++;
}
