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
    uint32_t previous_telemetry_us;
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
