#include "axis_state.h"
#include "open_foc.h"
#include "Xmath.h"

/* Commissioning defaults: tune against resolver noise and motor coast-down. */
#define STOP_SPEED_RPM 5.0f
#define STOP_STABLE_MS 200U
#define STOP_TIMEOUT_MS 10000U
#define ALIGN_TIME_MS 1200U
#define ALIGN_STABLE_MS 200U
#define ALIGN_TIMEOUT_MS 3000U
#define ALIGN_ANGLE_STEP_RAD 0.002f /* resolver radians per 1 ms */
#define POWER_STAGE_TIMEOUT_MS 2000U
#define PI_RAD 3.14159265359f

volatile AxisState foc_axis_state = AXIS_STATE_INITIALIZING;
volatile AxisState foc_axis_requested_state = AXIS_STATE_UNDEFINED;
volatile uint32_t foc_axis_error = AXIS_ERROR_NONE;
volatile uint32_t foc_axis_task_id = 0U;
volatile uint8_t foc_axis_clear_errors = 0U;
volatile uint8_t foc_axis_output_enabled = 0U;
volatile uint8_t foc_axis_power_stage_enabled = 0U;
volatile uint8_t foc_axis_calibrate_offset = 0U;
volatile uint8_t foc_axis_offset_valid = 0U;

static AxisState after_alignment;
static uint32_t state_elapsed_ms;
static uint32_t stable_ms;
static uint32_t power_wait_ms;
static float previous_angle;
static uint8_t previous_angle_valid;

/* All lifecycle transitions have one entry point. A restart resets timers too. */
static void enter_state(AxisState next)
{
    foc_axis_state = next;
    foc_axis_output_enabled = (next == AXIS_STATE_ENCODER_OFFSET_CALIBRATION ||
                              next == AXIS_STATE_CLOSED_LOOP_CONTROL ||
                              next == AXIS_STATE_OPEN_LOOP_TEST);
    state_elapsed_ms = 0U;
    stable_ms = 0U;
    power_wait_ms = 0U;
    previous_angle_valid = 0U;
}

static void fail(uint32_t error)
{
    foc_axis_error |= error;
    if (foc_axis_state != AXIS_STATE_FAULT)
        enter_state(AXIS_STATE_FAULT);
    foc_axis_output_enabled = 0U;
    foc_run_request = 0U;
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;
    foc_axis_offset_valid = 0U;
}

void AxisState_Init(void)
{
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;
    foc_axis_error = AXIS_ERROR_NONE;
    foc_axis_task_id = 0U;
    foc_axis_clear_errors = 0U;
    foc_axis_power_stage_enabled = 0U;
    foc_axis_calibrate_offset = 0U;
    foc_axis_offset_valid = 0U;
    after_alignment = AXIS_STATE_IDLE;
    previous_angle = 0.0f;
    enter_state(AXIS_STATE_INITIALIZING);
}

void AxisState_CheckSafety(uint8_t current_ready, uint8_t resolver_ready)
{
    uint32_t error = AXIS_ERROR_NONE;
    if (foc_axis_state == AXIS_STATE_FAULT || foc_axis_error != AXIS_ERROR_NONE) {
        fail(foc_axis_error);
        return;
    }
    if (foc_run_request == 0U) {
        if (foc_axis_state != AXIS_STATE_INITIALIZING && foc_axis_state != AXIS_STATE_IDLE)
            enter_state(AXIS_STATE_IDLE);
        return;
    }
    if (foc_axis_output_enabled || foc_axis_state == AXIS_STATE_WAIT_FOR_STOP) {
        if (!current_ready)
            error |= AXIS_ERROR_CURRENT_FEEDBACK;
        if (foc_axis_state != AXIS_STATE_OPEN_LOOP_TEST && !resolver_ready)
            error |= AXIS_ERROR_RESOLVER_FEEDBACK;
        if (error)
            fail(error);
    }
}

static uint8_t valid_motion(float speed, float angle)
{
    return !Value_isnan(speed) && !Value_isinf(speed) &&
           !Value_isnan(angle) && !Value_isinf(angle);
}

static void start_task(AxisState request, uint8_t resolver_ready)
{
    if (request == AXIS_STATE_OPEN_LOOP_TEST) {
        foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
        foc_control_state_active = foc_control_state_request;
        enter_state(AXIS_STATE_OPEN_LOOP_TEST);
    } else {
        if (!resolver_ready ||
            (foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST &&
             request != AXIS_STATE_ENCODER_OFFSET_CALIBRATION)) {
            fail(!resolver_ready ? AXIS_ERROR_RESOLVER_FEEDBACK : AXIS_ERROR_INVALID_STATE);
            return;
        }
        foc_control_state_active = foc_control_state_request;
        foc_axis_calibrate_offset = (request == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) ?
                                   1U : (foc_auto_calibrate_offset != 0U);
        foc_axis_offset_valid = 0U;
        after_alignment = request == AXIS_STATE_ENCODER_OFFSET_CALIBRATION ?
                          AXIS_STATE_IDLE : AXIS_STATE_CLOSED_LOOP_CONTROL;
        /* Remove excitation before checking coast-down, even on a live restart. */
        enter_state(AXIS_STATE_WAIT_FOR_STOP);
    }
    ++foc_axis_task_id;
}

void AxisState_Update1ms(uint8_t current_ready, uint8_t resolver_ready,
                        float speed_rpm, float resolver_angle_rad)
{
    AxisState request = foc_axis_requested_state;
    float angle_step;
    uint8_t motion_stable;
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;

    if (foc_axis_state == AXIS_STATE_FAULT) {
        if (foc_axis_clear_errors && !foc_run_request && current_ready &&
            (!(foc_axis_error & (AXIS_ERROR_RESOLVER_FEEDBACK |
                                 AXIS_ERROR_STOP_TIMEOUT | AXIS_ERROR_ALIGNMENT_UNSTABLE)) ||
             (resolver_ready && valid_motion(speed_rpm, resolver_angle_rad)))) {
            foc_axis_error = AXIS_ERROR_NONE;
            enter_state(AXIS_STATE_IDLE);
        }
        foc_axis_clear_errors = 0U;
        foc_run_request = 0U;
        return;
    }
    foc_axis_clear_errors = 0U;
    if (request == AXIS_STATE_IDLE) {
        foc_run_request = 0U;
        if (foc_axis_state != AXIS_STATE_IDLE)
            enter_state(AXIS_STATE_IDLE);
        return;
    }
    if ((request != AXIS_STATE_UNDEFINED && request != AXIS_STATE_STARTUP_SEQUENCE &&
         request != AXIS_STATE_ENCODER_OFFSET_CALIBRATION &&
         request != AXIS_STATE_CLOSED_LOOP_CONTROL && request != AXIS_STATE_OPEN_LOOP_TEST) ||
        (foc_control_state_request != FOC_CONTROL_CURRENT_LOOP &&
         foc_control_state_request != FOC_CONTROL_SPEED_CURRENT_LOOP &&
         foc_control_state_request != FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)) {
        fail(AXIS_ERROR_INVALID_STATE);
        return;
    }
    if (!current_ready && !foc_axis_output_enabled &&
        (foc_axis_state == AXIS_STATE_INITIALIZING || foc_axis_state == AXIS_STATE_IDLE)) {
        foc_axis_requested_state = request;
        return;
    }
    if (foc_axis_state == AXIS_STATE_INITIALIZING)
        enter_state(AXIS_STATE_IDLE);
    AxisState_CheckSafety(current_ready, resolver_ready);
    if (foc_axis_state == AXIS_STATE_FAULT)
        return;

    if (request != AXIS_STATE_UNDEFINED || (foc_run_request && foc_axis_state == AXIS_STATE_IDLE)) {
        foc_run_request = 1U;
        if (request == AXIS_STATE_STARTUP_SEQUENCE || request == AXIS_STATE_UNDEFINED)
            request = foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST ?
                      AXIS_STATE_OPEN_LOOP_TEST : AXIS_STATE_CLOSED_LOOP_CONTROL;
        start_task(request, resolver_ready);
        return;
    }
    if (!foc_run_request)
        return;
    if ((foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST) !=
        (foc_control_state_active == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)) {
        start_task(foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST ?
                   AXIS_STATE_OPEN_LOOP_TEST : AXIS_STATE_CLOSED_LOOP_CONTROL, resolver_ready);
        return;
    }
    foc_control_state_active = foc_control_state_request;
    if (foc_axis_state == AXIS_STATE_WAIT_FOR_STOP ||
        foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) {
        if (!valid_motion(speed_rpm, resolver_angle_rad)) {
            fail(AXIS_ERROR_RESOLVER_FEEDBACK);
            return;
        }
    }
    if (foc_axis_state == AXIS_STATE_WAIT_FOR_STOP) {
        ++state_elapsed_ms;
        /* Do not count a preceding powered interval as a stopped interval. */
        stable_ms = !foc_axis_power_stage_enabled && Value_fabsf(speed_rpm) <= STOP_SPEED_RPM ?
                    stable_ms + 1U : 0U;
        if (stable_ms >= STOP_STABLE_MS)
            enter_state(AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
        else if (state_elapsed_ms >= STOP_TIMEOUT_MS)
            fail(AXIS_ERROR_STOP_TIMEOUT);
        return;
    }
    if (!foc_axis_power_stage_enabled) {
        if (++power_wait_ms >= POWER_STAGE_TIMEOUT_MS)
            fail(AXIS_ERROR_POWER_STAGE_TIMEOUT);
        /* Alignment must be continuous after a lost enable acknowledgement. */
        state_elapsed_ms = 0U;
        stable_ms = 0U;
        previous_angle_valid = 0U;
        return;
    }
    power_wait_ms = 0U;
    if (foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) {
        ++state_elapsed_ms;
        angle_step = resolver_angle_rad - previous_angle;
        if (angle_step > PI_RAD) angle_step -= 2.0f * PI_RAD;
        if (angle_step < -PI_RAD) angle_step += 2.0f * PI_RAD;
        motion_stable = previous_angle_valid && Value_fabsf(speed_rpm) <= STOP_SPEED_RPM &&
                        Value_fabsf(angle_step) <= ALIGN_ANGLE_STEP_RAD;
        stable_ms = motion_stable ? stable_ms + 1U : 0U;
        previous_angle = resolver_angle_rad;
        previous_angle_valid = 1U;
        if (state_elapsed_ms >= ALIGN_TIME_MS && stable_ms >= ALIGN_STABLE_MS) {
            foc_axis_offset_valid = 1U;
            enter_state(after_alignment);
            if (after_alignment == AXIS_STATE_IDLE)
                foc_run_request = 0U;
        } else if (state_elapsed_ms >= ALIGN_TIMEOUT_MS) {
            fail(AXIS_ERROR_ALIGNMENT_UNSTABLE);
        }
    }
}
