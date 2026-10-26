#ifndef AXIS_STATE_H
#define AXIS_STATE_H

#include <stdint.h>

/* Single-axis lifecycle; control mode remains FOC_ControlState. */
typedef enum {
    AXIS_STATE_UNDEFINED = 0, /* request mailbox empty */
    AXIS_STATE_IDLE = 1,
    AXIS_STATE_ENCODER_OFFSET_CALIBRATION = 2,
    AXIS_STATE_CLOSED_LOOP_CONTROL = 3,
    AXIS_STATE_OPEN_LOOP_TEST = 4,
    AXIS_STATE_FAULT = 5,
    AXIS_STATE_STARTUP_SEQUENCE = 6, /* request only */
    AXIS_STATE_INITIALIZING = 7,
    AXIS_STATE_WAIT_FOR_STOP = 8
} AxisState;

enum {
    AXIS_ERROR_NONE = 0,
    AXIS_ERROR_INVALID_STATE = 1U << 0,
    AXIS_ERROR_CURRENT_FEEDBACK = 1U << 1,
    AXIS_ERROR_RESOLVER_FEEDBACK = 1U << 2,
    AXIS_ERROR_POWER_STAGE_TIMEOUT = 1U << 3,
    AXIS_ERROR_STOP_TIMEOUT = 1U << 4,
    AXIS_ERROR_ALIGNMENT_UNSTABLE = 1U << 5
};

extern volatile AxisState foc_axis_state;
extern volatile AxisState foc_axis_requested_state;
extern volatile uint32_t foc_axis_error;
/* Increments even when a new request restarts the same state. */
extern volatile uint32_t foc_axis_task_id;
extern volatile uint8_t foc_axis_clear_errors;
extern volatile uint8_t foc_axis_output_enabled;
/* Written by the board adapter after updating EN. */
extern volatile uint8_t foc_axis_power_stage_enabled;
extern volatile uint8_t foc_axis_calibrate_offset;
extern volatile uint8_t foc_axis_offset_valid;

void AxisState_Init(void);
void AxisState_Update1ms(uint8_t current_ready, uint8_t resolver_ready,
                           float speed_rpm, float resolver_angle_rad);
void AxisState_CheckSafety(uint8_t current_ready, uint8_t resolver_ready);

#endif
