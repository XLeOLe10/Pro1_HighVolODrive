#include "open_foc.h"
#include "tim.h"
#include "Xmath.h"
#include <stdint.h>

#define FOC_MOTOR_POLE_PAIRS          4.0f
#define FOC_RESOLVER_POLE_PAIRS       4.0f
#define FOC_POLE_PAIR_RATIO           (FOC_MOTOR_POLE_PAIRS / FOC_RESOLVER_POLE_PAIRS)

#define FOC_CURRENT_TS                0.00005f
#define FOC_SPEED_TS                  0.001f
#define FOC_ALIGN_U_DEFAULT           0.05f
#define FOC_ALIGN_U_MAX               0.05f
#define FOC_ALIGN_TIME_MS             1200U
#define FOC_SPEED_RAMP_RPM_PER_MS     0.20f  /* 200 rpm/s: 0 -> 100 rpm in about 0.5 s */
#define FOC_SPEED_TEST_STEP_RPM       25.0f

#define FOC_U_LIMIT                   1.0f
#define FOC_CURRENT_PI_U_LIMIT_DEFAULT 0.50f
#define FOC_CURRENT_PI_I_LIMIT_DEFAULT 0.50f
#define FOC_CURRENT_TEST_REF_LIMIT_A   0.30f
#define FOC_DUTY_MIN                  0.05f
#define FOC_DUTY_MAX                  0.95f
#define FOC_SQRT3_BY_2                0.86602540378f
#define FOC_TWO_PI                    6.28318530718f

#define FOC_POLE_EST_MIN_SPEED_RPM    5.0f
#define FOC_POLE_EST_TARGET_BAND_RPM  0.5f
#define FOC_POLE_EST_SPEED_STEP_RPM   2.0f
#define FOC_POLE_EST_INTEGER_BAND     0.25f
#define FOC_POLE_EST_FILTER_ALPHA     0.01f
#define FOC_POLE_EST_VALID_TIME_MS    1000U

#define FOC_SWAP_VW                   0U

static float theta_e;
static float resolver_angle_feedback_rad;
static float resolver_speed_feedback_rpm;
static float id_feedback_a;
static float iq_feedback_a;
static float ramped_speed_ref_rpm;
static float open_loop_theta_e;
static uint8_t motion_feedback_valid;
static uint8_t current_feedback_valid;
static uint8_t foc_enable;
static uint8_t run_was_requested;
static uint32_t align_ms_left;
static uint16_t pole_estimate_stable_ms;
static float pole_estimate_last_speed_rpm;

volatile uint8_t foc_run_request = 0U;
volatile FOC_ControlState foc_control_state_request = FOC_CONTROL_CURRENT_LOOP;
volatile FOC_ControlState foc_control_state_active = FOC_CONTROL_CURRENT_LOOP;

volatile float foc_du = 0.5f;
volatile float foc_dv = 0.5f;
volatile float foc_dw = 0.5f;
volatile float foc_theta = 0.0f;
volatile float foc_current_rpm = 0.0f;
volatile uint8_t foc_aligning = 1U;
/* Adjustable in Keil Watch.  Start conservatively at 24 V because the former
   fixed 0.04 command caused an AD2S1210 input-clipping fault during alignment. */
volatile float foc_align_u_norm = FOC_ALIGN_U_DEFAULT;
volatile uint8_t foc_auto_calibrate_offset = 1U;
volatile float foc_resolver_direction = -1.0f;
/* Positive Iq drives this motor in the direction reported as negative by the
   raw AD2S speed.  Invert speed only so the outer speed loop is negative
   feedback; keep the already verified electrical-angle direction unchanged. */
volatile float foc_speed_feedback_direction = -1.0f;
volatile float foc_theta_offset_rad = 0.0f;

/* Current-loop values retain the user's present bring-up settings. */
volatile float foc_id_ref_a = 0.0f;
volatile float foc_current_loop_iq_ref_a = 0.0f;
volatile float foc_current_loop_uq_ff_norm = 0.0f;
volatile float foc_kp_d = 0.3f;
volatile float foc_ki_d = 0.01f;
volatile float foc_kp_q = 0.35f;
volatile float foc_ki_q = 0.08f;

volatile float foc_iq_ref_a = 0.0f;
volatile float foc_ud_pi = 0.0f;
volatile float foc_uq_pi = 0.0f;
volatile float foc_uq_ff = 0.0f;
volatile float foc_uq_open_loop_norm = 0.08f;
volatile float foc_id_integrator = 0.0f;
volatile float foc_iq_integrator = 0.0f;
volatile float foc_current_pi_u_limit_norm = FOC_CURRENT_PI_U_LIMIT_DEFAULT;
volatile float foc_current_pi_i_limit_norm = FOC_CURRENT_PI_I_LIMIT_DEFAULT;
volatile float foc_current_test_ref_limit_a = FOC_CURRENT_TEST_REF_LIMIT_A;

/* Conservative defaults for the first 50 rpm speed-loop test. */
volatile float foc_speed_target_rpm = 50.0f;
volatile float foc_speed_ref_rpm = 0.0f;
volatile float foc_speed_feedback_rpm = 0.0f;
volatile float foc_kp_speed = 0.15f;
volatile float foc_ki_speed = 0.08f;
volatile float foc_speed_integrator = 0.0f;
volatile float foc_speed_pi_iq_ref_a = 0.0f;
volatile float foc_speed_iq_limit_a = 0.30f;
/* Write +1/-1 in Watch for one +25/-25 rpm target step.  The command is
   consumed and cleared automatically by the 1 ms task. */
volatile int8_t foc_speed_step_command = 0;

/* State 2 is a temporary resolver/pole-pair commissioning mode.  The
   electrical command is generated with the candidate pole-pair count while
   AD2S1210 supplies mechanical rpm (its 4X resolver ratio is already removed
   in AD2S1210_VelocityToMechanicalRPM). */
volatile float foc_open_loop_test_speed_rpm = 50.0f;
volatile float foc_open_loop_test_pole_pairs = 4.0f;
volatile float foc_open_loop_electrical_rpm = 0.0f;
volatile float foc_pole_pair_estimate_raw = 0.0f;
volatile float foc_pole_pair_estimate = 0.0f;
volatile uint8_t foc_pole_pair_estimate_valid = 0U;

static float FOC_Direction(void)
{
    return (foc_resolver_direction < 0.0f) ? -1.0f : 1.0f;
}

static float FOC_Abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

static FOC_ControlState FOC_SanitizeState(FOC_ControlState state)
{
    if ((state == FOC_CONTROL_SPEED_CURRENT_LOOP) ||
        (state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST))
    {
        return state;
    }
    return FOC_CONTROL_CURRENT_LOOP;
}

static void FOC_ResetCurrentPI(void)
{
    foc_id_integrator = 0.0f;
    foc_iq_integrator = 0.0f;
    foc_ud_pi = 0.0f;
    foc_uq_pi = 0.0f;
}

static void FOC_ResetSpeedPI(void)
{
    foc_speed_integrator = 0.0f;
    foc_speed_pi_iq_ref_a = 0.0f;
}

static void FOC_ResetPolePairEstimator(void)
{
    pole_estimate_stable_ms = 0U;
    pole_estimate_last_speed_rpm = 0.0f;
    foc_pole_pair_estimate_raw = 0.0f;
    foc_pole_pair_estimate = 0.0f;
    foc_pole_pair_estimate_valid = 0U;
}

static void FOC_RampSpeed(float target_rpm)
{
    if (ramped_speed_ref_rpm < target_rpm)
    {
        ramped_speed_ref_rpm += FOC_SPEED_RAMP_RPM_PER_MS;
        if (ramped_speed_ref_rpm > target_rpm)
        {
            ramped_speed_ref_rpm = target_rpm;
        }
    }
    else if (ramped_speed_ref_rpm > target_rpm)
    {
        ramped_speed_ref_rpm -= FOC_SPEED_RAMP_RPM_PER_MS;
        if (ramped_speed_ref_rpm < target_rpm)
        {
            ramped_speed_ref_rpm = target_rpm;
        }
    }
}

static void FOC_UpdatePolePairEstimator(void)
{
    float mechanical_rpm = FOC_Abs(foc_speed_feedback_rpm);
    float electrical_rpm = FOC_Abs(foc_open_loop_electrical_rpm);
    float target_error = FOC_Abs(ramped_speed_ref_rpm -
                                 foc_open_loop_test_speed_rpm);
    float speed_step = FOC_Abs(mechanical_rpm -
                               pole_estimate_last_speed_rpm);
    float estimate;
    uint16_t nearest_integer;

    pole_estimate_last_speed_rpm = mechanical_rpm;

    if ((motion_feedback_valid == 0U) ||
        (mechanical_rpm < FOC_POLE_EST_MIN_SPEED_RPM) ||
        (target_error > FOC_POLE_EST_TARGET_BAND_RPM) ||
        ((pole_estimate_stable_ms != 0U) &&
         (speed_step > FOC_POLE_EST_SPEED_STEP_RPM)))
    {
        pole_estimate_stable_ms = 0U;
        foc_pole_pair_estimate_valid = 0U;
        return;
    }

    /* p_motor = electrical speed / mechanical speed.  For the default test:
       50 rpm * 4 candidate pole pairs = 200 electrical rpm; 200 / 50 = 4. */
    estimate = electrical_rpm / mechanical_rpm;
    foc_pole_pair_estimate_raw = estimate;

    if (pole_estimate_stable_ms == 0U)
    {
        foc_pole_pair_estimate = estimate;
    }
    else
    {
        foc_pole_pair_estimate += FOC_POLE_EST_FILTER_ALPHA *
                                  (estimate - foc_pole_pair_estimate);
    }

    if (pole_estimate_stable_ms < FOC_POLE_EST_VALID_TIME_MS)
    {
        pole_estimate_stable_ms++;
    }
    if (pole_estimate_stable_ms >= FOC_POLE_EST_VALID_TIME_MS)
    {
        nearest_integer = (uint16_t)(foc_pole_pair_estimate + 0.5f);
        foc_pole_pair_estimate_valid =
            (FOC_Abs(foc_pole_pair_estimate - (float)nearest_integer) <=
             FOC_POLE_EST_INTEGER_BAND) ? 1U : 0U;
    }
}

static void FOC_WriteDuty(float du, float dv, float dw)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t ccr1;
    uint32_t ccr2;
    uint32_t ccr3;

    du = Value_Limit(du, FOC_DUTY_MAX, FOC_DUTY_MIN);
    dv = Value_Limit(dv, FOC_DUTY_MAX, FOC_DUTY_MIN);
    dw = Value_Limit(dw, FOC_DUTY_MAX, FOC_DUTY_MIN);

    ccr1 = (uint32_t)((float)(arr + 1U) * du);
    ccr2 = (uint32_t)((float)(arr + 1U) * dv);
    ccr3 = (uint32_t)((float)(arr + 1U) * dw);

    if (ccr1 > arr) ccr1 = arr;
    if (ccr2 > arr) ccr2 = arr;
    if (ccr3 > arr) ccr3 = arr;

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccr2);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ccr3);
}

static void FOC_WritePhaseDuty(float du, float dv, float dw)
{
#if FOC_SWAP_VW
    FOC_WriteDuty(du, dw, dv);
#else
    FOC_WriteDuty(du, dv, dw);
#endif
}

static void FOC_UpdateDaxisPI(void)
{
    float error;
    float output;
    float u_limit = Value_Limit(FOC_Abs(foc_current_pi_u_limit_norm),
                                FOC_U_LIMIT, 0.0f);
    float i_limit = Value_Limit(FOC_Abs(foc_current_pi_i_limit_norm),
                                FOC_U_LIMIT, 0.0f);
    float ref_limit = FOC_Abs(foc_current_test_ref_limit_a);
    float id_reference = foc_id_ref_a;

    if (current_feedback_valid == 0U)
    {
        foc_ud_pi = 0.0f;
        return;
    }

    if (foc_control_state_active == FOC_CONTROL_CURRENT_LOOP)
    {
        id_reference = Value_Limit(id_reference, ref_limit, -ref_limit);
    }
    error = id_reference - id_feedback_a;
    output = foc_kp_d * error + foc_id_integrator;

    if ((output < u_limit && output > -u_limit) ||
        (output >= u_limit && error < 0.0f) ||
        (output <= -u_limit && error > 0.0f))
    {
        foc_id_integrator += foc_ki_d * FOC_CURRENT_TS * error;
        foc_id_integrator = Value_Limit(foc_id_integrator,
                                        i_limit,
                                        -i_limit);
        output = foc_kp_d * error + foc_id_integrator;
    }

    foc_ud_pi = Value_Limit(output, u_limit, -u_limit);
}

static void FOC_UpdateQaxisPI(void)
{
    float error;
    float output;
    float u_limit = Value_Limit(FOC_Abs(foc_current_pi_u_limit_norm),
                                FOC_U_LIMIT, 0.0f);
    float i_limit = Value_Limit(FOC_Abs(foc_current_pi_i_limit_norm),
                                FOC_U_LIMIT, 0.0f);

    if (current_feedback_valid == 0U)
    {
        foc_uq_pi = 0.0f;
        return;
    }

    error = foc_iq_ref_a - iq_feedback_a;
    output = foc_kp_q * error + foc_iq_integrator;

    if ((output < u_limit && output > -u_limit) ||
        (output >= u_limit && error < 0.0f) ||
        (output <= -u_limit && error > 0.0f))
    {
        foc_iq_integrator += foc_ki_q * FOC_CURRENT_TS * error;
        foc_iq_integrator = Value_Limit(foc_iq_integrator,
                                        i_limit,
                                        -i_limit);
        output = foc_kp_q * error + foc_iq_integrator;
    }

    foc_uq_pi = Value_Limit(output, u_limit, -u_limit);
}

static void FOC_UpdateSpeedPI(void)
{
    float error;
    float output;
    float iq_limit = foc_speed_iq_limit_a;

    if (iq_limit < 0.0f)
    {
        iq_limit = -iq_limit;
    }

    if (iq_limit <= 0.0f)
    {
        FOC_ResetSpeedPI();
        return;
    }

    error = foc_speed_ref_rpm - foc_speed_feedback_rpm;
    output = foc_kp_speed * error + foc_speed_integrator;

    if ((output < iq_limit && output > -iq_limit) ||
        (output >= iq_limit && error < 0.0f) ||
        (output <= -iq_limit && error > 0.0f))
    {
        foc_speed_integrator += foc_ki_speed * FOC_SPEED_TS * error;
        foc_speed_integrator = Value_Limit(foc_speed_integrator,
                                           iq_limit,
                                           -iq_limit);
        output = foc_kp_speed * error + foc_speed_integrator;
    }

    foc_speed_pi_iq_ref_a = Value_Limit(output, iq_limit, -iq_limit);
}

static void FOC_DqToDuty(float ud, float uq, float angle,
                         float *du, float *dv, float *dw)
{
    float sin_e;
    float cos_e;
    float u_alpha;
    float u_beta;
    float ua;
    float ub;
    float uc;
    float u_max;
    float u_min;
    float offset;

    ud = Value_Limit(ud, FOC_U_LIMIT, -FOC_U_LIMIT);
    uq = Value_Limit(uq, FOC_U_LIMIT, -FOC_U_LIMIT);
    fast_sin_cos(angle, &sin_e, &cos_e);
    ipark(&u_alpha, &u_beta, ud, uq, sin_e, cos_e);

    ua = u_alpha;
    ub = -0.5f * u_alpha + FOC_SQRT3_BY_2 * u_beta;
    uc = -0.5f * u_alpha - FOC_SQRT3_BY_2 * u_beta;

    u_max = ua;
    if (ub > u_max) u_max = ub;
    if (uc > u_max) u_max = uc;
    u_min = ua;
    if (ub < u_min) u_min = ub;
    if (uc < u_min) u_min = uc;

    offset = 0.5f - 0.5f * (u_max + u_min);
    *du = ua + offset;
    *dv = ub + offset;
    *dw = uc + offset;
}

void OpenFOC_Init(void)
{
    theta_e = 0.0f;
    resolver_angle_feedback_rad = 0.0f;
    resolver_speed_feedback_rpm = 0.0f;
    id_feedback_a = 0.0f;
    iq_feedback_a = 0.0f;
    ramped_speed_ref_rpm = 0.0f;
    open_loop_theta_e = 0.0f;
    motion_feedback_valid = 0U;
    current_feedback_valid = 0U;
    foc_enable = 1U;
    run_was_requested = 0U;
    align_ms_left = FOC_ALIGN_TIME_MS;
    foc_run_request = 0U;
    foc_control_state_request = FOC_CONTROL_CURRENT_LOOP;
    foc_control_state_active = FOC_CONTROL_CURRENT_LOOP;
    foc_aligning = 1U;
    foc_iq_ref_a = 0.0f;
    foc_uq_ff = 0.0f;
    foc_speed_ref_rpm = 0.0f;
    foc_current_rpm = 0.0f;
    foc_open_loop_electrical_rpm = 0.0f;
    FOC_ResetCurrentPI();
    FOC_ResetSpeedPI();
    FOC_ResetPolePairEstimator();
    FOC_WriteDuty(0.5f, 0.5f, 0.5f);
}

void OpenFOC_SetTargetRPM(float rpm)
{
    if (rpm > 1000.0f) rpm = 1000.0f;
    if (rpm < -1000.0f) rpm = -1000.0f;
    foc_speed_target_rpm = rpm;
}

void OpenFOC_SetVoltageNorm(float voltage_norm)
{
    OpenFOC_SetUqVoltageNorm(voltage_norm);
}

void OpenFOC_SetUqVoltageNorm(float uq_norm)
{
    foc_current_loop_uq_ff_norm = Value_Limit(uq_norm, FOC_U_LIMIT, 0.0f);
    foc_uq_open_loop_norm = foc_current_loop_uq_ff_norm;
}

void OpenFOC_SetRotorFeedback(float resolver_angle_rad,
                              float resolver_speed_rpm,
                              uint8_t valid)
{
    float direction = FOC_Direction();
    float speed_direction =
        (foc_speed_feedback_direction < 0.0f) ? -1.0f : 1.0f;

    resolver_angle_feedback_rad = resolver_angle_rad;
    resolver_speed_feedback_rpm = resolver_speed_rpm;
    motion_feedback_valid = valid;
    foc_speed_feedback_rpm = speed_direction * resolver_speed_feedback_rpm;

    /* In state 2, foc_theta must remain the commanded open-loop field angle;
       resolver position is observed only and must not overwrite that angle. */
    if (foc_control_state_active != FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
    {
        theta_e = Value_normalize(direction * resolver_angle_feedback_rad *
                                  FOC_POLE_PAIR_RATIO + foc_theta_offset_rad);
        foc_theta = theta_e;
    }
}

void OpenFOC_Stop(void)
{
    foc_run_request = 0U;
    foc_enable = 0U;
    run_was_requested = 0U;
    align_ms_left = FOC_ALIGN_TIME_MS;
    ramped_speed_ref_rpm = 0.0f;
    foc_speed_ref_rpm = 0.0f;
    foc_current_rpm = 0.0f;
    foc_open_loop_electrical_rpm = 0.0f;
    foc_iq_ref_a = 0.0f;
    foc_uq_ff = 0.0f;
    foc_aligning = 0U;
    FOC_ResetCurrentPI();
    FOC_ResetSpeedPI();
    FOC_WriteDuty(0.5f, 0.5f, 0.5f);
}

void OpenFOC_Ramp_1ms(void)
{
    FOC_ControlState requested_state;
    int8_t speed_step_command;

    if (foc_enable == 0U)
    {
        return;
    }

    speed_step_command = foc_speed_step_command;
    if (speed_step_command != 0)
    {
        foc_speed_step_command = 0;
        if (speed_step_command > 0)
        {
            OpenFOC_SetTargetRPM(foc_speed_target_rpm +
                                 FOC_SPEED_TEST_STEP_RPM);
        }
        else
        {
            OpenFOC_SetTargetRPM(foc_speed_target_rpm -
                                 FOC_SPEED_TEST_STEP_RPM);
        }
    }

    requested_state = FOC_SanitizeState(foc_control_state_request);

    if (foc_run_request == 0U)
    {
        run_was_requested = 0U;
        align_ms_left = FOC_ALIGN_TIME_MS;
        foc_aligning = 1U;
        ramped_speed_ref_rpm = 0.0f;
        foc_speed_ref_rpm = 0.0f;
        foc_current_rpm = 0.0f;
        foc_open_loop_electrical_rpm = 0.0f;
        foc_iq_ref_a = 0.0f;
        foc_uq_ff = 0.0f;
        FOC_ResetCurrentPI();
        FOC_ResetSpeedPI();
        return;
    }

    if (run_was_requested == 0U)
    {
        run_was_requested = 1U;
        foc_control_state_active = requested_state;
        if (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
        {
            /* Pure open-loop bring-up: apply Uq at commanded angle and ramp
               immediately.  No resolver-based alignment is required. */
            align_ms_left = 0U;
            foc_aligning = 0U;
        }
        else
        {
            align_ms_left = FOC_ALIGN_TIME_MS;
            foc_aligning = 1U;
        }
        ramped_speed_ref_rpm =
            (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST) ?
            0.0f : foc_speed_feedback_rpm;
        open_loop_theta_e = 0.0f;
        FOC_ResetCurrentPI();
        FOC_ResetSpeedPI();
        if (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
        {
            /* A new commissioning run must earn a fresh valid estimate. */
            FOC_ResetPolePairEstimator();
        }
    }

    if (align_ms_left > 0U)
    {
        align_ms_left--;
        foc_aligning = 1U;
        foc_iq_ref_a = 0.0f;
        foc_uq_ff = 0.0f;
        foc_open_loop_electrical_rpm = 0.0f;
        if (align_ms_left == 0U)
        {
            if (foc_control_state_active ==
                FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
            {
                /* State 2 is pure commanded-angle operation.  Resolver angle,
                   validity and fault state never affect its starting angle. */
                theta_e = 0.0f;
                foc_theta = 0.0f;
                ramped_speed_ref_rpm = 0.0f;
                open_loop_theta_e = 0.0f;
            }
            else
            {
                if (foc_auto_calibrate_offset != 0U)
                {
                    foc_theta_offset_rad = Value_normalize(
                        -FOC_Direction() * resolver_angle_feedback_rad *
                        FOC_POLE_PAIR_RATIO);
                }
                theta_e = Value_normalize(
                    FOC_Direction() * resolver_angle_feedback_rad *
                    FOC_POLE_PAIR_RATIO + foc_theta_offset_rad);
                foc_theta = theta_e;
                ramped_speed_ref_rpm = foc_speed_feedback_rpm;
                open_loop_theta_e = 0.0f;
            }
            foc_aligning = 0U;
        }
        return;
    }

    foc_aligning = 0U;

    if (requested_state != foc_control_state_active)
    {
        foc_control_state_active = requested_state;
        ramped_speed_ref_rpm =
            (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST) ?
            0.0f : foc_speed_feedback_rpm;
        open_loop_theta_e =
            (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST) ?
            0.0f : theta_e;
        FOC_ResetCurrentPI();
        FOC_ResetSpeedPI();
        if (requested_state == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
        {
            FOC_ResetPolePairEstimator();
        }
    }

    if (foc_control_state_active == FOC_CONTROL_SPEED_CURRENT_LOOP)
    {
        FOC_RampSpeed(foc_speed_target_rpm);

        foc_speed_ref_rpm = ramped_speed_ref_rpm;
        foc_current_rpm = ramped_speed_ref_rpm;
        foc_open_loop_electrical_rpm = 0.0f;
        FOC_UpdateSpeedPI();
        foc_iq_ref_a = foc_speed_pi_iq_ref_a;
        foc_uq_ff = 0.0f;
    }
    else if (foc_control_state_active == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
    {
        FOC_RampSpeed(foc_open_loop_test_speed_rpm);
        foc_speed_ref_rpm = ramped_speed_ref_rpm;
        foc_current_rpm = ramped_speed_ref_rpm;
        foc_open_loop_electrical_rpm = ramped_speed_ref_rpm *
                                       FOC_Abs(foc_open_loop_test_pole_pairs);
        foc_iq_ref_a = 0.0f;
        foc_uq_ff = foc_uq_open_loop_norm;
        FOC_ResetCurrentPI();
        FOC_ResetSpeedPI();
        FOC_UpdatePolePairEstimator();
    }
    else
    {
        FOC_ResetSpeedPI();
        foc_speed_ref_rpm = 0.0f;
        foc_current_rpm = 0.0f;
        foc_open_loop_electrical_rpm = 0.0f;
        foc_iq_ref_a = Value_Limit(
            foc_current_loop_iq_ref_a,
            FOC_Abs(foc_current_test_ref_limit_a),
            -FOC_Abs(foc_current_test_ref_limit_a));
        foc_uq_ff = foc_current_loop_uq_ff_norm;
    }
}

void OpenFOC_SetIdFeedback(float id_a, uint8_t valid)
{
    id_feedback_a = id_a;
    current_feedback_valid = valid;
}

void OpenFOC_SetCurrentFeedback(float id_a, float iq_a, uint8_t valid)
{
    id_feedback_a = id_a;
    iq_feedback_a = iq_a;
    current_feedback_valid = valid;
}

void OpenFOC_Run_20kHz(void)
{
    float du = 0.5f;
    float dv = 0.5f;
    float dw = 0.5f;

    if ((foc_enable == 0U) || (foc_run_request == 0U) ||
        (current_feedback_valid == 0U))
    {
        foc_du = 0.5f;
        foc_dv = 0.5f;
        foc_dw = 0.5f;
        FOC_WriteDuty(0.5f, 0.5f, 0.5f);
        return;
    }

    if (align_ms_left > 0U)
    {
        FOC_ResetCurrentPI();
        theta_e = 0.0f;
        foc_theta = 0.0f;
        FOC_DqToDuty(Value_Limit(foc_align_u_norm,
                                 FOC_ALIGN_U_MAX,
                                 0.0f),
                     0.0f, 0.0f, &du, &dv, &dw);
    }
    else if (foc_control_state_active == FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST)
    {
        FOC_ResetCurrentPI();
        open_loop_theta_e = Value_normalize(
            open_loop_theta_e + foc_open_loop_electrical_rpm *
            (FOC_TWO_PI / 60.0f) * FOC_CURRENT_TS);
        theta_e = open_loop_theta_e;
        foc_theta = theta_e;
        FOC_DqToDuty(0.0f, foc_uq_open_loop_norm,
                     theta_e, &du, &dv, &dw);
    }
    else
    {
        theta_e = Value_normalize(FOC_Direction() * resolver_angle_feedback_rad *
                                  FOC_POLE_PAIR_RATIO + foc_theta_offset_rad);
        foc_theta = theta_e;
        FOC_UpdateDaxisPI();
        FOC_UpdateQaxisPI();
        FOC_DqToDuty(foc_ud_pi, foc_uq_ff + foc_uq_pi,
                     theta_e, &du, &dv, &dw);
    }

    foc_du = du;
    foc_dv = dv;
    foc_dw = dw;
    FOC_WritePhaseDuty(du, dv, dw);
}

void OpenFOC_Run_1ms(void)
{
    OpenFOC_Ramp_1ms();
}
