#include <stdio.h>
#include <stdlib.h>
#include "tim.h"
#include "open_foc.h"

TIM_HandleTypeDef htim1 = {2499U, {0U, 0U, 0U}};
static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); \
    exit(1); } } while (0)

/* Synthetic software fixture, never motor commissioning parameters.
 * Actual production open_foc.c, Xmath.c and axis_command_core.c are linked.
 * These tests bypass the gateway deliberately to exercise only its validated
 * FOC adapter; they do not grant target output permission. */
static AxisCommandFrame frame(float rpm, float iq)
{
    AxisCommandFrame f = {1U, 1U, AXIS_CMD_START, AXIS_CMD_SPEED_MODE,
                          0.0f, 0.0f, 1U, 101U};
    f.speed_target_rpm = rpm;
    f.iq_limit_a = iq;
    return f;
}

static void feedback(void)
{
    OpenFOC_SetRotorFeedback(0.0f, 0.0f, 1U);
    OpenFOC_SetCurrentFeedback(0.0f, 0.0f, 1U);
}

static void neutral(void)
{
    CHECK(htim1.ccr[0] == 1250U);
    CHECK(htim1.ccr[1] == 1250U);
    CHECK(htim1.ccr[2] == 1250U);
}

static void legacy_writes_without_command(void)
{
    unsigned int mode;
    OpenFOC_Init();
    OpenFOC_ApplyCommand(NULL);
    feedback();
    for (mode = 0U; mode < 3U; mode++) {
        foc_run_request = 1U;
        foc_speed_step_command = 1;
        foc_speed_target_rpm = 900.0f;
        foc_speed_iq_limit_a = 9.0f;
        foc_control_state_request = (FOC_ControlState)mode;
        foc_control_state_active = (FOC_ControlState)mode;
        OpenFOC_Ramp_1ms();
        OpenFOC_EnforceCommandLimit();
        OpenFOC_Run_20kHz();
        neutral();
        CHECK(foc_iq_ref_a == 0.0f);
        CHECK(foc_speed_step_command == 0);
        OpenFOC_ApplyCommand(NULL);
        CHECK(foc_run_request == 0U);
        CHECK(foc_speed_target_rpm == 0.0f);
        CHECK(foc_speed_iq_limit_a == 0.0f);
    }
}

static void legacy_writes_do_not_override_active(void)
{
    unsigned int n;
    AxisCommandFrame f = frame(10.0f, 0.25f);
    OpenFOC_Init();
    OpenFOC_ApplyCommand(&f);
    feedback();
    for (n = 0U; n < 1200U; n++) OpenFOC_Ramp_1ms();
    CHECK(foc_aligning == 0U);
    foc_run_request = 0U;
    foc_speed_step_command = -1;
    foc_speed_target_rpm = -900.0f;
    foc_speed_iq_limit_a = 9.0f;
    foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    foc_control_state_active = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    /* Real runtime reapplies the same complete active frame before slow/fast. */
    OpenFOC_ApplyCommand(&f);
    OpenFOC_Ramp_1ms();
    OpenFOC_EnforceCommandLimit();
    CHECK(foc_run_request == 1U);
    CHECK(foc_speed_target_rpm == 10.0f);
    CHECK(foc_speed_ref_rpm > 0.0f);
    CHECK(foc_speed_iq_limit_a == 0.25f);
    CHECK(foc_control_state_active == FOC_CONTROL_SPEED_CURRENT_LOOP);
    CHECK(foc_speed_step_command == 0);
    CHECK(foc_iq_ref_a <= 0.25f && foc_iq_ref_a >= -0.25f);
}

static void stop_start_without_slow_tick(void)
{
    unsigned int n;
    AxisCommandFrame a = frame(10.0f, 0.5f);
    AxisCommandFrame b = frame(-20.0f, 0.25f);
    OpenFOC_Init();
    feedback();
    OpenFOC_ApplyCommand(&a);
    for (n = 0U; n < 1201U; n++) OpenFOC_Ramp_1ms();
    CHECK(foc_aligning == 0U);
    foc_speed_integrator = 0.4f;
    foc_speed_pi_iq_ref_a = 0.4f;
    foc_id_integrator = 0.4f;
    foc_iq_integrator = 0.4f;
    OpenFOC_ApplyCommand(NULL); /* STOP at a non-slow cycle. */
    CHECK(foc_speed_integrator == 0.0f);
    CHECK(foc_speed_pi_iq_ref_a == 0.0f);
    CHECK(foc_id_integrator == 0.0f);
    CHECK(foc_iq_integrator == 0.0f);
    CHECK(foc_run_request == 0U);
    neutral();
    /* New START at the next fast cycle: no Ramp_1ms call between. */
    OpenFOC_ApplyCommand(&b);
    CHECK(foc_aligning == 1U);
    CHECK(foc_run_request == 1U);
    CHECK(foc_speed_integrator == 0.0f);
    CHECK(foc_speed_ref_rpm == 0.0f);
    OpenFOC_EnforceCommandLimit();
    OpenFOC_Run_20kHz();
    CHECK(htim1.ccr[0] != htim1.ccr[1]); /* Alignment computation restarted. */
    CHECK(foc_iq_ref_a == 0.0f);
    CHECK(foc_speed_target_rpm == -20.0f);
}

static void limit_drop_without_slow_tick(void)
{
    AxisCommandFrame a = frame(10.0f, 2.0f);
    AxisCommandFrame b = frame(20.0f, 0.25f);
    OpenFOC_Init();
    OpenFOC_ApplyCommand(&a);
    foc_iq_ref_a = 1.5f;
    foc_speed_pi_iq_ref_a = -1.5f;
    foc_speed_integrator = 1.5f;
    OpenFOC_ApplyCommand(&b);
    OpenFOC_EnforceCommandLimit(); /* No slow PI update has occurred. */
    CHECK(foc_iq_ref_a == 0.25f);
    CHECK(foc_speed_pi_iq_ref_a == -0.25f);
    CHECK(foc_speed_integrator == 0.25f);
    CHECK(foc_speed_target_rpm == 20.0f);
    b.iq_limit_a = 0.0f;
    OpenFOC_ApplyCommand(&b);
    OpenFOC_EnforceCommandLimit();
    CHECK(foc_iq_ref_a == 0.0f);
    CHECK(foc_speed_pi_iq_ref_a == 0.0f);
    CHECK(foc_speed_integrator == 0.0f);
}

int main(void)
{
    legacy_writes_without_command();
    legacy_writes_do_not_override_active();
    stop_start_without_slow_tick();
    limit_drop_without_slow_tick();
    printf("PASS: %u FOC command integration assertions; CPU register stubs only\n", checks);
    return 0;
}
