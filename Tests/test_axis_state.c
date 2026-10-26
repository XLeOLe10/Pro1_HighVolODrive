#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "axis_state.h"
#include "open_foc.h"
#include "current_monitor.h"
#include "current_sense.h"
#include "Xmath.h"
#include "tim.h"

TIM_HandleTypeDef htim1 = {999U, {0U, 0U, 0U}};
static uint8_t sample_available;
static uint8_t resolver_ready;
static uint8_t board_available;
static float resolver_speed;
static float resolver_angle;
static uint16_t raw_v;
static uint16_t raw_w;

uint8_t CurrentSense_HasNewSample(void) { return sample_available; }
void CurrentSense_GetRaw(uint16_t *u, uint16_t *v, uint16_t *w, uint16_t *bus)
{
    *u = 2048U; *v = raw_v; *w = raw_w; *bus = 100U;
}

/* Same acquisition/state/angle/transform/output order as the TIM1 ISR. */
static void cycle(uint8_t lifecycle)
{
    OpenFOC_SetRotorFeedback(resolver_angle, resolver_speed, resolver_ready);
    CurrentMonitor_Acquire20kHz();
    OpenFOC_SetCurrentFeedback(monitor_id_a, monitor_iq_a, CurrentMonitor_IsSampleValid());
    if (lifecycle)
        OpenFOC_Ramp_1ms();
    OpenFOC_PrepareAngle20kHz();
    CurrentMonitor_Transform(foc_theta);
    OpenFOC_SetCurrentFeedback(monitor_id_a, monitor_iq_a, CurrentMonitor_IsSampleValid());
    OpenFOC_Run_20kHz();
    foc_axis_power_stage_enabled = board_available && foc_run_request && foc_axis_output_enabled;
}

static void tick(void)
{
    int i;
    cycle(1U);
    for (i = 1; i < 20; ++i) cycle(0U);
}

static void boot(void)
{
    sample_available = 1U;
    resolver_ready = 1U;
    board_available = 1U;
    resolver_speed = 0.0f;
    resolver_angle = 0.4f;
    raw_v = raw_w = 2048U;
    foc_auto_calibrate_offset = 1U;
    foc_resolver_direction = 1.0f;
    foc_speed_feedback_direction = 1.0f;
    foc_theta_offset_rad = 0.0f;
    foc_id_ref_a = 0.0f;
    foc_current_loop_iq_ref_a = 0.0f;
    OpenFOC_Init();
    CurrentMonitor_Init();
}

static void reset(void)
{
    int i;
    boot();
    for (i = 0; i < 6097; ++i) cycle(0U);
    tick();
    assert(CurrentMonitor_IsReady() && CurrentMonitor_IsSampleValid());
    assert(foc_axis_state == AXIS_STATE_IDLE);
}

static void wait_stopped(void)
{
    int i;
    assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP);
    for (i = 0; i < 199; ++i) {
        tick();
        assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP);
        assert(!foc_axis_output_enabled);
    }
    tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
}

static void finish_alignment(void)
{
    int i;
    for (i = 0; i < 1199; ++i) {
        tick();
        assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION);
        assert(foc_theta == 0.0f);
    }
    tick();
}

static void start_closed(void)
{
    foc_run_request = 1U;
    tick();
    wait_stopped();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
}

int main(void)
{
    int i;
    float sine, cosine, id, iq, old_theta;
    uint32_t sequence;

    /* An early startup request survives ADC offset calibration. */
    boot();
    sample_available = 0U;
    foc_axis_requested_state = AXIS_STATE_STARTUP_SEQUENCE;
    for (i = 0; i < 10; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_INITIALIZING && !foc_axis_output_enabled);
    sample_available = 1U;
    for (i = 0; i < 306; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP);
    for (i = 0; i < 201 && foc_axis_state == AXIS_STATE_WAIT_FOR_STOP; ++i) tick();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);

    /* ADC freshness is independent of the persistent offset-ready flag. */
    reset();
    start_closed();
    sequence = monitor_sample_sequence;
    sample_available = 0U;
    cycle(0U);
    assert(CurrentMonitor_IsReady() && !CurrentMonitor_IsSampleValid());
    assert(monitor_sample_age_cycles == 1U && monitor_sample_sequence == sequence);
    assert(foc_axis_error & AXIS_ERROR_CURRENT_FEEDBACK);
    assert(!foc_run_request && !foc_axis_power_stage_enabled);
    cycle(0U);
    assert(monitor_sample_age_cycles == 2U);
    foc_axis_clear_errors = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_FAULT);
    sample_available = 1U;
    foc_axis_clear_errors = 1U;
    foc_axis_requested_state = AXIS_STATE_CLOSED_LOOP_CONTROL;
    tick();
    assert(foc_axis_state == AXIS_STATE_IDLE && !foc_run_request);
    assert(monitor_sample_age_cycles == 0U);

    /* A rotating fan coasts with outputs disabled before alignment. */
    reset();
    resolver_speed = 100.0f;
    foc_run_request = 1U;
    tick();
    for (i = 0; i < 1000; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP && !foc_axis_power_stage_enabled);
    resolver_speed = 0.0f;
    wait_stopped();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);

    reset();
    resolver_speed = 100.0f;
    foc_run_request = 1U;
    for (i = 0; i < 10001; ++i) tick();
    assert(foc_axis_error & AXIS_ERROR_STOP_TIMEOUT);

    /* Time alone cannot validate alignment when angle is still moving. */
    reset();
    foc_run_request = 1U;
    tick();
    wait_stopped();
    for (i = 0; i < 3000; ++i) {
        resolver_angle = Value_normalize(resolver_angle + 0.02f);
        tick();
    }
    assert(foc_axis_error & AXIS_ERROR_ALIGNMENT_UNSTABLE);
    assert(!foc_axis_offset_valid && !foc_axis_power_stage_enabled);

    /* Angle wrap does not look like a full-revolution jump. */
    reset();
    foc_run_request = 1U;
    tick();
    wait_stopped();
    for (i = 0; i < 1200; ++i) {
        resolver_angle = i % 2 ? 0.0001f : 6.2830853f;
        tick();
    }
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);

    /* Nonzero measured currents use the updated angle on the transition cycle. */
    reset();
    foc_run_request = 1U;
    tick();
    wait_stopped();
    for (i = 0; i < 1199; ++i) tick();
    raw_v = 2060U;
    raw_w = 2030U;
    cycle(1U);
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    assert(fabsf(foc_theta_offset_rad - (6.28318530718f - 0.4f)) < 0.001f);
    fast_sin_cos(foc_theta, &sine, &cosine);
    park(&id, &iq, monitor_i_alpha_a, monitor_i_beta_a, sine, cosine);
    assert(fabsf(monitor_id_a - id) < 0.0001f && fabsf(monitor_iq_a - iq) < 0.0001f);
    old_theta = foc_theta;
    OpenFOC_Run_20kHz();
    assert(foc_theta == old_theta); /* output does not silently change the angle */

    /* Current/speed mode switch keeps lifecycle; restart re-enters coast-down. */
    foc_control_state_request = FOC_CONTROL_SPEED_CURRENT_LOOP;
    tick();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    resolver_speed = 50.0f;
    foc_axis_requested_state = AXIS_STATE_CLOSED_LOOP_CONTROL;
    tick();
    assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP && !foc_axis_power_stage_enabled);
    OpenFOC_Stop();
    cycle(0U);
    assert(foc_axis_state == AXIS_STATE_IDLE && !foc_axis_output_enabled);
    tick();

    /* Open-loop allows missing resolver feedback, but not missing ADC data. */
    reset();
    resolver_ready = 0U;
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    assert(foc_axis_state == AXIS_STATE_OPEN_LOOP_TEST && foc_axis_power_stage_enabled);
    foc_pole_pair_estimate_valid = 1U;
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    assert(foc_axis_task_id == 2U && !foc_pole_pair_estimate_valid);
    sample_available = 0U;
    cycle(0U);
    assert(foc_axis_error & AXIS_ERROR_CURRENT_FEEDBACK);

    reset();
    foc_axis_requested_state = AXIS_STATE_OPEN_LOOP_TEST;
    tick();
    resolver_speed = 70.0f;
    foc_control_state_request = FOC_CONTROL_SPEED_CURRENT_LOOP;
    tick();
    assert(foc_axis_state == AXIS_STATE_WAIT_FOR_STOP && !foc_axis_power_stage_enabled);

    /* Calibration-only finishes idle, including when mode 2 was selected. */
    reset();
    foc_auto_calibrate_offset = 0U;
    foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    foc_axis_requested_state = AXIS_STATE_ENCODER_OFFSET_CALIBRATION;
    tick();
    wait_stopped();
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_IDLE && foc_axis_offset_valid && !foc_run_request);

    /* Power acknowledgement cannot be replaced by elapsed time. */
    reset();
    board_available = 0U;
    foc_run_request = 1U;
    tick();
    wait_stopped();
    for (i = 0; i < 1500; ++i) tick();
    assert(foc_axis_state == AXIS_STATE_ENCODER_OFFSET_CALIBRATION && !foc_axis_offset_valid);
    board_available = 1U;
    cycle(0U);
    finish_alignment();
    assert(foc_axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL);
    reset();
    board_available = 0U;
    foc_run_request = 1U;
    tick();
    wait_stopped();
    for (i = 0; i < 2000; ++i) tick();
    assert(foc_axis_error & AXIS_ERROR_POWER_STAGE_TIMEOUT);

    /* Invalid requests and resolver loss are rejected and latched. */
    reset();
    foc_control_state_request = (FOC_ControlState)99;
    tick();
    assert(foc_axis_error & AXIS_ERROR_INVALID_STATE);
    reset();
    foc_axis_requested_state = (AxisState)99;
    tick();
    assert(foc_axis_error & AXIS_ERROR_INVALID_STATE);
    reset();
    start_closed();
    resolver_ready = 0U;
    cycle(0U);
    assert(foc_axis_error & AXIS_ERROR_RESOLVER_FEEDBACK);
    foc_axis_clear_errors = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_FAULT);
    resolver_ready = 1U;
    foc_axis_clear_errors = 1U;
    tick();
    assert(foc_axis_state == AXIS_STATE_IDLE && !foc_run_request);
    reset();
    foc_run_request = 1U;
    tick();
    resolver_speed = NAN;
    tick();
    assert(foc_axis_error & AXIS_ERROR_RESOLVER_FEEDBACK);

    reset();
    start_closed();
    resolver_angle = NAN;
    cycle(0U);
    assert(foc_axis_error & AXIS_ERROR_RESOLVER_FEEDBACK);
    assert(isfinite(foc_theta) && !foc_axis_power_stage_enabled);

    puts("PASS: ADC freshness, coast-down, stable alignment, angle coherence and lifecycle regressions");
    return 0;
}
