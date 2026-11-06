#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include <stdint.h>

void AppRuntime_Init(void);           /* main, before scheduler */
void AppRuntime_TaskStart(void);      /* appTask, once */
void AppRuntime_BackgroundStep(void); /* appTask, once per scheduled period */
void AppRuntime_FastISR(void);        /* TIM1 IRQ0, no RTOS API */

extern volatile uint32_t app_task_iterations;
extern volatile uint32_t app_task_deadline_misses;
extern volatile uint32_t app_task_last_cycles;
extern volatile uint32_t app_task_max_cycles;
extern volatile uint32_t app_stack_min_free_bytes;
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
