#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include <stdint.h>
#include "board_config.h"

#if AXIS_COMMAND_BENCH_ENABLE
/* One frozen real-gap event. All durations are elapsed time including IRQs.
 * The four previous_* fields belong to ONE previous task iteration, not maxima.
 * Until captured=1, UINT32_MAX in these four fields means NOT CAPTURED.
 * Diagnostic only: never use this data for control/output permission. */
typedef struct {
    uint32_t captured;
    uint32_t previous_total_us;
    uint32_t previous_acquisition_us;
    uint32_t previous_telemetry_us; /* appTask enqueue time on the Bench */
    uint32_t previous_diagnostics_us;
    uint32_t sample_interval_us;
    uint32_t between_tasks_us;
    uint32_t current_entry_to_sample_us;
    uint32_t misses_since_previous_body;
    uint32_t previous_iteration;
    uint32_t gap_count;
    uint32_t previous_stack_scan_ran;
    uint32_t dwt_ready;
} AppGapTrace;
extern volatile AppGapTrace app_gap_trace;
extern volatile uint32_t app_stack_probe_enabled;
/* Idle hook only. Attempts at most one pending Bench telemetry frame. */
void AppRuntime_ServiceIdleTelemetry(void);

#define APP_READY_LOSS_POSITION  (1UL << 0)
#define APP_READY_LOSS_SPEED     (1UL << 1)
#define APP_READY_LOSS_DWT_AGE   (1UL << 2)
#define APP_READY_LOSS_HAL_AGE   (1UL << 3)
#define APP_READY_LOSS_CURRENT   (1UL << 4)
#define APP_READY_LOSS_LOCKOUT   (1UL << 5)
#define APP_READY_LOSS_FAULT     (1UL << 6)
/* First active-command NOT_READY revoke, frozen by TIM1 after disarm.
 * Ages refer to the exact feedback copy used for that ready decision.
 * task_phase is a coarse task marker: 0 unmarked work/waiting, 1 acquisition,
 * 2 CAN, 3 telemetry enqueue, 4 diagnostics; it is not a PC or a SAMPLE-edge record.
 * Counters are observations at capture, not proof of this event's cause.
 * Read only when captured=1; no field is an input to control or recovery. */
typedef struct {
    uint32_t captured;
    uint32_t reason_bits;
    uint32_t age_cycles;
    uint32_t age_ms;
    uint32_t sample_seq;
    uint32_t command_seq;
    uint32_t epoch_before;
    uint32_t epoch_after;
    uint32_t task_phase;
    uint32_t gap_count;
    uint32_t spi_errors;
    uint32_t deadline_misses;
} AppReadyLossTrace;
extern volatile AppReadyLossTrace app_ready_loss_trace;

enum {
    APP_TASK_INTERVAL_WAITING = 0,
    APP_TASK_INTERVAL_COMPLETE,
    APP_TASK_INTERVAL_UNAVAILABLE
};
/* One appTask-owned partition of the edge interval in ad2s_age_trace.
 * State COMPLETE publishes all three durations. UINT32_MAX means unavailable.
 * Between-step elapsed time includes scheduling, loop overhead and preemption. */
typedef struct {
    uint32_t state;
    uint32_t old_edge_to_end_cycles;
    uint32_t between_steps_cycles;
    uint32_t entry_to_edge_cycles;
    /* Subsets of old_edge_to_end_cycles in the same source step. Both stay
     * UINT32_MAX if inconsistent. Zero queue time is expected when telemetry was not due. */
    uint32_t old_edge_to_acquisition_end_cycles;
    uint32_t old_telemetry_queue_cycles;
} AppTaskIntervalTrace;
extern volatile AppTaskIntervalTrace app_task_interval_trace;
#endif

/* Startup identity: immutable after the single pre-scheduler attempt.
 * Diagnostic mirrors are NOT authority; changing Watch values cannot create
 * a valid identity. The getter returns the private state and four words, or
 * four zeros when invalid. No RNG access, allocation, IRQ masking or retries. */
#define APP_BOOT_STATE_NOT_READY 0U
#define APP_BOOT_STATE_VALID     1U
#define APP_BOOT_STATE_FAILED    2U
#define APP_BOOT_ERROR_CONFIG    (1UL << 0)
#define APP_BOOT_ERROR_CLOCK     (1UL << 1)
#define APP_BOOT_ERROR_SEED      (1UL << 2)
#define APP_BOOT_ERROR_TIMEOUT   (1UL << 3)
#define APP_BOOT_ERROR_HAL       (1UL << 4)
#define APP_BOOT_ERROR_REPEAT    (1UL << 5)
#define APP_BOOT_ERROR_ZERO      (1UL << 6)
extern volatile uint32_t app_boot_identity_state;
extern volatile uint32_t app_boot_error_flags;
uint32_t AppRuntime_GetBootIdentity(uint32_t words[4]);

void AppRuntime_Init(void);           /* main, before scheduler */
void AppRuntime_TaskStart(void);      /* appTask, once */
void AppRuntime_BackgroundStep(void); /* appTask, once per scheduled period */
void AppRuntime_FastISR(void);        /* TIM1 IRQ0, no RTOS API */

extern volatile uint32_t app_task_iterations;
extern volatile uint32_t app_task_deadline_misses;
extern volatile uint32_t app_task_last_cycles;
extern volatile uint32_t app_task_max_cycles;
/* Idle-owned, best-effort diagnostics; no control decisions use these.
 * UINT32_MAX/count=0 means unmeasured; a stale timestamp means no recent scan. */
extern volatile uint32_t app_stack_min_free_bytes;
extern volatile uint32_t app_stack_probe_count;
extern volatile uint32_t app_stack_probe_last_tick_ms;
extern volatile uint32_t app_heap_free_bytes;
extern volatile uint32_t app_heap_min_free_bytes;
extern volatile uint32_t app_feedback_invalid_count;
extern volatile uint32_t app_adc_missed_count;
extern volatile uint32_t app_start_rejected_count;
extern volatile uint32_t app_critical_max_cycles;
extern volatile uint32_t foc_isr_last_cycles;
extern volatile uint32_t foc_isr_max_cycles;
extern volatile uint32_t foc_isr_budget_cycles;
extern volatile uint32_t foc_isr_overrun_count;
extern volatile uint32_t current_telemetry_tx_count;
extern volatile uint32_t current_telemetry_drop_count;
#endif
