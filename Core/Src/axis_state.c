#include "axis_state.h"
#include "open_foc.h"

#define ALIGN_TIME_MS 1200U
#define POWER_STAGE_TIMEOUT_MS 2000U

volatile AxisState foc_axis_state = AXIS_STATE_INITIALIZING;
volatile AxisState foc_axis_requested_state = AXIS_STATE_UNDEFINED;
volatile uint32_t foc_axis_error = AXIS_ERROR_NONE;
volatile uint32_t foc_axis_task_id = 0U;
volatile uint8_t foc_axis_clear_errors = 0U;
volatile uint8_t foc_axis_output_enabled = 0U;
volatile uint8_t foc_axis_power_stage_enabled = 0U;
volatile uint8_t foc_axis_calibrate_offset = 0U;
volatile uint8_t foc_axis_offset_valid = 0U;

/* A calibration task advances to either CLOSED_LOOP_CONTROL or IDLE. */
static AxisState after_alignment;
static uint32_t align_elapsed_ms;
static uint32_t power_wait_ms;

static void enter_idle(void)
{
    foc_axis_state = AXIS_STATE_IDLE;
    foc_axis_output_enabled = 0U;
    align_elapsed_ms = 0U;
    power_wait_ms = 0U;
}

static void fail(uint32_t error)
{
    foc_axis_error |= error;
    foc_axis_state = AXIS_STATE_FAULT;
    foc_axis_output_enabled = 0U;
    foc_run_request = 0U;
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;
    foc_axis_offset_valid = 0U;
}

void AxisState_Init(void)
{
    foc_axis_state = AXIS_STATE_INITIALIZING;
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;
    foc_axis_error = AXIS_ERROR_NONE;
    foc_axis_task_id = 0U;
    foc_axis_clear_errors = 0U;
    foc_axis_output_enabled = 0U;
    foc_axis_power_stage_enabled = 0U;
    foc_axis_calibrate_offset = 0U;
    foc_axis_offset_valid = 0U;
    after_alignment = AXIS_STATE_IDLE;
    align_elapsed_ms = 0U;
    power_wait_ms = 0U;
}

void AxisState_CheckSafety(uint8_t current_ready, uint8_t resolver_ready)
{
    if ((foc_axis_state == AXIS_STATE_FAULT) ||
        (foc_axis_error != AXIS_ERROR_NONE)) {
        fail(foc_axis_error);
        return;
    }
    if (foc_run_request == 0U) {
        if (foc_axis_state != AXIS_STATE_INITIALIZING)
            enter_idle();
        foc_axis_output_enabled = 0U;
        return;
    }
    if (foc_axis_output_enabled != 0U) {
        if (current_ready == 0U)
            fail(AXIS_ERROR_CURRENT_FEEDBACK);
        else if ((foc_axis_state != AXIS_STATE_OPEN_LOOP_TEST) &&
                 (resolver_ready == 0U))
            fail(AXIS_ERROR_RESOLVER_FEEDBACK);
    }
}

static void start_task(AxisState request, uint8_t resolver_ready)
{
    if (request == AXIS_STATE_OPEN_LOOP_TEST) {
        foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
        foc_control_state_active = foc_control_state_request;
        foc_axis_state = AXIS_STATE_OPEN_LOOP_TEST;
    } else {
        if ((resolver_ready == 0U) ||
            (foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST &&
             request != AXIS_STATE_ENCODER_OFFSET_CALIBRATION)) {
            fail(resolver_ready == 0U ? AXIS_ERROR_RESOLVER_FEEDBACK :
                                      AXIS_ERROR_INVALID_STATE);
            return;
        }
        foc_control_state_active = foc_control_state_request;
        foc_axis_calibrate_offset =
            (request == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) ? 1U :
            (foc_auto_calibrate_offset != 0U);
        foc_axis_offset_valid = 0U;
        after_alignment = (request == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) ?
            AXIS_STATE_IDLE : AXIS_STATE_CLOSED_LOOP_CONTROL;
        foc_axis_state = AXIS_STATE_ENCODER_OFFSET_CALIBRATION;
    }
    align_elapsed_ms = 0U;
    power_wait_ms = 0U;
    foc_axis_output_enabled = 1U;
    ++foc_axis_task_id;
}

void AxisState_Update1ms(uint8_t current_ready, uint8_t resolver_ready)
{
    AxisState request = foc_axis_requested_state;
    foc_axis_requested_state = AXIS_STATE_UNDEFINED;

    /* Clearing a fault never starts a motor. A fresh request is required. */
    if (foc_axis_state == AXIS_STATE_FAULT) {
        if ((foc_axis_clear_errors != 0U) && (foc_run_request == 0U) &&
            (current_ready != 0U) &&
            (((foc_axis_error & AXIS_ERROR_RESOLVER_FEEDBACK) == 0U) ||
             (resolver_ready != 0U))) {
            foc_axis_error = AXIS_ERROR_NONE;
            enter_idle();
        }
        foc_axis_clear_errors = 0U;
        foc_run_request = 0U;
        return;
    }
    foc_axis_clear_errors = 0U;
    if (request == AXIS_STATE_IDLE) {
        foc_run_request = 0U;
        enter_idle();
        return;
    }
    if ((request != AXIS_STATE_UNDEFINED) &&
        (request != AXIS_STATE_STARTUP_SEQUENCE) &&
        (request != AXIS_STATE_ENCODER_OFFSET_CALIBRATION) &&
        (request != AXIS_STATE_CLOSED_LOOP_CONTROL) &&
        (request != AXIS_STATE_OPEN_LOOP_TEST)) {
        fail(AXIS_ERROR_INVALID_STATE);
        return;
    }
    if ((foc_control_state_request != FOC_CONTROL_CURRENT_LOOP) &&
        (foc_control_state_request != FOC_CONTROL_SPEED_CURRENT_LOOP) &&
        (foc_control_state_request != FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)) {
        fail(AXIS_ERROR_INVALID_STATE);
        return;
    }
    /* Hold a request until the existing ADC zero-offset acquisition is ready. */
    if (current_ready == 0U && foc_axis_output_enabled == 0U) {
        foc_axis_requested_state = request;
        return;
    }
    if (foc_axis_state == AXIS_STATE_INITIALIZING)
        enter_idle();
    AxisState_CheckSafety(current_ready, resolver_ready);
    if (foc_axis_state == AXIS_STATE_FAULT)
        return;

    if (request != AXIS_STATE_UNDEFINED) {
        foc_run_request = 1U;
        if (request == AXIS_STATE_STARTUP_SEQUENCE)
            request = foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST ?
                AXIS_STATE_OPEN_LOOP_TEST : AXIS_STATE_CLOSED_LOOP_CONTROL;
        start_task(request, resolver_ready);
        return;
    }
    if (foc_run_request == 0U)
        return;
    if (foc_axis_state == AXIS_STATE_IDLE) {
        start_task(foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST ?
                   AXIS_STATE_OPEN_LOOP_TEST : AXIS_STATE_CLOSED_LOOP_CONTROL,
                   resolver_ready);
        return;
    }
    /* Crossing between open/closed loop always repeats the entry sequence. */
    if ((foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST) !=
        (foc_control_state_active == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)) {
        start_task(foc_control_state_request == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST ?
                   AXIS_STATE_OPEN_LOOP_TEST : AXIS_STATE_CLOSED_LOOP_CONTROL,
                   resolver_ready);
        return;
    }
    foc_control_state_active = foc_control_state_request;
    if (foc_axis_power_stage_enabled == 0U) {
        if (++power_wait_ms >= POWER_STAGE_TIMEOUT_MS)
            fail(AXIS_ERROR_POWER_STAGE_TIMEOUT);
        return;
    }
    power_wait_ms = 0U;
    if (foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION) {
        if (++align_elapsed_ms >= ALIGN_TIME_MS) {
            foc_axis_offset_valid = 1U;
            foc_axis_state = after_alignment;
            if (after_alignment == AXIS_STATE_IDLE) {
                foc_run_request = 0U;
                foc_axis_output_enabled = 0U;
            }
        }
    }
}
