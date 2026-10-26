#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "axis_state.h"
#include "open_foc.h"
#include "tim.h"

TIM_HandleTypeDef htim1 = {999U, {0U, 0U, 0U}};
static uint8_t current_ready;
static uint8_t resolver_ready;
static uint8_t board_available;

static void fast_cycle(void)
{
    OpenFOC_SetRotorFeedback(0.4f, 0.0f, resolver_ready);
    OpenFOC_SetCurrentFeedback(0.0f, 0.0f, current_ready);
    OpenFOC_Run_20kHz();
    foc_axis_power_stage_enabled = board_available && foc_run_request &&
                                   foc_axis_output_enabled;
}

static void tick(void)
{
    int i;
    OpenFOC_SetRotorFeedback(0.4f, 0.0f, resolver_ready);
    OpenFOC_SetCurrentFeedback(0.0f, 0.0f, current_ready);
    OpenFOC_Ramp_1ms();
    for (i = 0; i < 20; ++i)
        fast_cycle();
}

static void reset(void)
{
    current_ready = 1U;
    resolver_ready = 1U;
    board_available = 1U;
    foc_auto_calibrate_offset = 1U;
    foc_resolver_direction = 1.0f;
    foc_speed_feedback_direction = 1.0f;
    OpenFOC_Init();
}

static void finish_alignment(void)
{
    int i;
    for (i = 0; i < 1199; ++i) {
        tick();
        assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
    }
    tick();
}

int main(void)
{
    int i;
    /* Boot and retained startup request wait for ADC offsets without output. */
    reset();
    current_ready = 0U;
    foc_axis_requested_state = AXIS_STATE_STARTUP_SEQUENCE;
    for (i = 0; i < 10; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_INITIALIZING);
    assert(!foc_axis_output_enabled);
    current_ready = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
    assert(foc_axis_requested_state == AXIS_STATE_UNDEFINED);
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    assert(foc_axis_offset_valid && !foc_aligning);
    assert(fabsf(foc_theta_offset_rad - (6.28318530718f - 0.4f)) < 0.001f);

    /* Current/speed mode switch clears integrators without repeating alignment. */
    foc_speed_integrator = 1.0f;
    foc_control_state_request = FOC_CONTROL_SPEED_CURRENT_LOOP;
    tick();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    assert(foc_control_state_active == FOC_CONTROL_SPEED_CURRENT_LOOP);
    assert(fabsf(foc_speed_integrator) < 0.1f);

    /* Fast feedback loss disarms before the next 1 ms lifecycle tick. */
    resolver_ready = 0U;
    fast_cycle();
    assert(foc_axis_state == AXIS_STATE_FAULT);
    assert(foc_axis_error & AXIS_ERROR_RESOLVER_FEEDBACK);
    assert(!foc_run_request && !foc_axis_power_stage_enabled);
    assert(htim1.compare[0] == 500U);
    foc_axis_clear_errors = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_FAULT);
    resolver_ready = 1U;
    foc_axis_clear_errors = 1U;
    foc_axis_requested_state = AXIS_STATE_CLOSED_LOOP_CONTROL;
    tick();
    assert(foc_axis_state == AXIS_STATE_IDLE && !foc_run_request);
    tick();
    assert(foc_axis_state == AXIS_STATE_IDLE);

    /* Open-loop commissioning ignores resolver health, but never current health. */
    resolver_ready = 0U;
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    assert(foc_axis_state == AXIS_STATE_OPEN_LOOP_TEST);
    assert(!foc_aligning && foc_axis_power_stage_enabled);
    current_ready = 0U;
    fast_cycle();
    assert(foc_axis_error & AXIS_ERROR_CURRENT_FEEDBACK);
    assert(!foc_axis_output_enabled);

    /* Repeating a test request must restart its angle and pole estimator. */
    reset();
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    foc_pole_pair_estimate_valid = 1U;
    foc_pole_pair_estimate = 17.0f;
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    assert(foc_axis_task_id == 2U);
    assert(!foc_pole_pair_estimate_valid && foc_pole_pair_estimate == 0.0f);

    /* An invalid mode is rejected instead of silently running current control. */
    reset();
    foc_control_state_request = (FOC_ControlState)99;
    tick();
    assert(foc_axis_error & AXIS_ERROR_INVALID_STATE);
    reset();
    foc_axis_requested_state = (AxisState)99;
    tick();
    assert(foc_axis_error & AXIS_ERROR_INVALID_STATE);

    /* Stop cancels calibration, and the legacy run flag can start it again. */
    reset();
    foc_run_request = 1U;
    tick();
    assert(foc_aligning);
    OpenFOC_Stop();
    fast_cycle();
    assert(!foc_axis_output_enabled && !foc_axis_offset_valid);
    tick();
    foc_run_request = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);

    /* Calibration alone updates offset then returns idle, even in test mode. */
    reset();
    foc_auto_calibrate_offset = 0U;
    foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    foc_axis_requested_state = AXIS_STATE_ENCODER_OFFSET_CALIBRATION;
    tick();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_IDLE && !foc_run_request);
    assert(foc_axis_offset_valid);
    assert(fabsf(foc_theta_offset_rad - (6.28318530718f - 0.4f)) < 0.001f);

    /* An unavailable power stage cannot consume the alignment duration. */
    reset();
    board_available = 0U;
    foc_run_request = 1U;
    tick();
    for (i = 0; i < 1500; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
    assert(!foc_axis_offset_valid);
    board_available = 1U;
    fast_cycle();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    reset();
    board_available = 0U;
    foc_run_request = 1U;
    for (i = 0; i < 2001; ++i) tick();
    assert(foc_axis_error & AXIS_ERROR_POWER_STAGE_TIMEOUT);

    /* An open-to-closed transition requires a new alignment and healthy resolver. */
    reset();
    foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    foc_run_request = 1U;
    tick();
    foc_control_state_request = FOC_CONTROL_SPEED_CURRENT_LOOP;
    tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    foc_axis_requested_state = AXIS_STATE_IDLE;
    tick();
    assert(!foc_axis_output_enabled && !foc_run_request);
    puts("PASS: single-axis lifecycle and FOC integration scenarios");
    return 0;
}
