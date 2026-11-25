#ifndef APP_POLICY_H
#define APP_POLICY_H
#include <stdint.h>

/* Initial software timing contract; verify delivery latency on the board. */
#define APP_PERIOD_TICKS          1U
#define APP_FEEDBACK_MAX_AGE_MS    2U
#define APP_TELEMETRY_PERIOD_MS    5U
#define APP_DIAGNOSTIC_PERIOD_MS   1000U

/* Unsigned, wrap-safe for deadlines less than 2^31 ticks apart.
 * Arguments must be side-effect-free local values. Used by compile-time cases. */
#define APP_TIME_REACHED(now, deadline) \
    ((uint32_t)((uint32_t)(now) - (uint32_t)(deadline)) < 0x80000000UL)
#define APP_NEXT_WAKE(now, deadline) \
    (APP_TIME_REACHED((now), (deadline)) ? \
     (uint32_t)((uint32_t)(now) + APP_PERIOD_TICKS) : (uint32_t)(deadline))
#define APP_FEEDBACK_FRESH(now_ms, sample_ms, now_cycles, sample_cycles, limit_cycles) \
    (((uint32_t)((uint32_t)(now_ms) - (uint32_t)(sample_ms)) <= APP_FEEDBACK_MAX_AGE_MS) && \
     ((uint32_t)((uint32_t)(now_cycles) - (uint32_t)(sample_cycles)) <= (uint32_t)(limit_cycles)))
#define APP_CAN_ENABLE(run, prepared, calibrated, feedback, fault, lockout) \
    (((run) != 0U) && ((prepared) != 0U) && ((calibrated) != 0U) && \
     ((feedback) != 0U) && ((fault) == 0U) && ((lockout) == 0U))

#endif
