#ifndef AXIS_COMMAND_BENCH_H
#define AXIS_COMMAND_BENCH_H
#include <stdint.h>

/* One-shot task-side exerciser. HAL/TIM7 milliseconds only; no sleep/spin.
 * Hardware output isolation is a separate compile-time/runtime board guard.
 * These scalar Watch values never authorize commands or outputs. */
enum {
    AXIS_BENCH_WAIT_READY = 0U,
    AXIS_BENCH_WAIT_A = 10U,
    AXIS_BENCH_WAIT_B = 20U,
    AXIS_BENCH_WAIT_STOP = 30U,
    AXIS_BENCH_REJECT_OLD = 40U,
    AXIS_BENCH_WAIT_EXPIRING_START = 50U,
    AXIS_BENCH_WAIT_EXPIRY = 60U,
    AXIS_BENCH_WAIT_IDLE = 70U,
    AXIS_BENCH_PASS = 100U,
    AXIS_BENCH_FAIL = 200U
};
enum {
    AXIS_BENCH_ERR_NONE = 0U,
    AXIS_BENCH_ERR_OUTPUT = 1U,
    AXIS_BENCH_ERR_FAULT = 2U,
    AXIS_BENCH_ERR_OVERRUN = 3U,
    AXIS_BENCH_ERR_READY_TIMEOUT = 4U,
    AXIS_BENCH_ERR_READY_LOST = 5U,
    AXIS_BENCH_ERR_SNAPSHOT = 6U,
    AXIS_BENCH_ERR_SUBMIT = 7U,
    AXIS_BENCH_ERR_ACK_TIMEOUT = 8U,
    AXIS_BENCH_ERR_FRAME = 9U,
    AXIS_BENCH_ERR_STOP = 10U,
    AXIS_BENCH_ERR_OLD_EPOCH = 11U,
    AXIS_BENCH_ERR_EXPIRY = 12U,
    AXIS_BENCH_ERR_RESTART = 13U,
    AXIS_BENCH_ERR_TIM1_STALLED = 14U,
    AXIS_BENCH_ERR_REJECTED = 15U
};
extern volatile uint32_t axis_bench_stage;
extern volatile uint32_t axis_bench_pass_mask;
extern volatile uint32_t axis_bench_fail_code;
void AxisCommandBench_Init(uint32_t now_ms);
void AxisCommandBench_Step(uint32_t now_ms, uint32_t ready,
                           uint32_t output_safe, uint32_t fault_mask,
                           uint32_t overruns);
#endif
