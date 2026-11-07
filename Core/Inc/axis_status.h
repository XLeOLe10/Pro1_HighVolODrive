#ifndef AXIS_STATUS_H
#define AXIS_STATUS_H
#include <stdint.h>

/* Diagnostic observations only: never use this word to authorize outputs.
 * Independent C implementation; reference/scope: docs/specs/002-axis-status-observer.md.
 * Read once, then decode the local copy for a coherent state/reason pair. */
enum {
    AXIS_OBS_BOOT = 0U,
    AXIS_OBS_CALIBRATING = 1U, /* ADC current offsets only. */
    AXIS_OBS_IDLE = 2U,
    AXIS_OBS_ALIGNING = 3U,
    AXIS_OBS_RUNNING = 4U, /* Software output enabled, not proof of rotation. */
    AXIS_OBS_FAULT = 5U
};
enum {
    AXIS_INHIBIT_PWM_NOT_PREPARED = 0x001U,
    AXIS_INHIBIT_CURRENT_OFFSETS = 0x002U,
    AXIS_INHIBIT_POSITION = 0x004U,
    AXIS_INHIBIT_SPEED = 0x008U,
    AXIS_INHIBIT_FEEDBACK_AGE = 0x010U,
    AXIS_INHIBIT_ADC_FRAME = 0x020U,
    AXIS_INHIBIT_TEST_LOCKOUT = 0x040U,
    AXIS_INHIBIT_FAULT = 0x080U,
    AXIS_INHIBIT_POWER_DISABLED = 0x100U
};

#define AXIS_STATUS_STATE_MASK 0xffUL
#define AXIS_STATUS_STATE(word) ((uint32_t)(word) & AXIS_STATUS_STATE_MASK)
#define AXIS_STATUS_INHIBITS(word) ((uint32_t)(word) >> 8U)
#define AXIS_STATUS_PACK(state, inhibits) \
    (AXIS_STATUS_STATE(state) | ((uint32_t)(inhibits) << 8U))
/* Arguments must be side-effect-free values. These are observation policies,
 * shared with compile-time checks, not a second control state machine. */
#define AXIS_STATUS_PHASE(enabled, aligning, inhibits) \
    ((((inhibits) & AXIS_INHIBIT_FAULT) != 0U) ? AXIS_OBS_FAULT : \
     ((enabled) != 0U) ? (((aligning) != 0U) ? AXIS_OBS_ALIGNING : AXIS_OBS_RUNNING) : \
     (((inhibits) & AXIS_INHIBIT_CURRENT_OFFSETS) != 0U) ? AXIS_OBS_CALIBRATING : AXIS_OBS_IDLE)
#define AXIS_STATUS_KEEP_FAULT(previous, inhibits) \
    ((uint32_t)(inhibits) | (AXIS_STATUS_INHIBITS(previous) & AXIS_INHIBIT_FAULT))
#define AXIS_STATUS_NEXT(previous, enabled, aligning, inhibits) \
    AXIS_STATUS_PACK(AXIS_STATUS_PHASE((enabled), (aligning), \
        AXIS_STATUS_KEEP_FAULT((previous), (inhibits))), \
        AXIS_STATUS_KEEP_FAULT((previous), (inhibits)))
#define AXIS_STATUS_ON_FAULT(previous) \
    AXIS_STATUS_PACK(AXIS_OBS_FAULT, AXIS_STATUS_INHIBITS(previous) | AXIS_INHIBIT_FAULT)

extern volatile uint32_t axis_status_word;
/* Writers hold the existing PRIMASK section; no HAL/RTOS or allocation here.
 * Normal publisher: TIM1, AFTER BoardSafety_SetOutputsEnabled.
 * Fault publisher: BoardSafety_LatchFault, AFTER hardware disable/latch. */
void AxisStatus_Publish(uint32_t enabled, uint32_t aligning, uint32_t inhibits);
void AxisStatus_LatchFault(void);
#endif
