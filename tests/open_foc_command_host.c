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


/* 006 phase contract. These checks inspect actual FOC private-state projections,
 * never a replacement state model. 1200 below is a slow-call count, not a
 * simulated wall-clock or physical-alignment guarantee. */
static void expect_phase(FOC_RunPhase phase)
{
    CHECK(OpenFOC_GetRunPhase() == phase);
    CHECK(foc_aligning == (phase == FOC_PHASE_ALIGNING ? 1U : 0U));
}

static void run_phase_idle_and_mirror(void)
{
    unsigned int n;
    OpenFOC_Init();
    expect_phase(FOC_PHASE_IDLE);
    neutral();
    for (n = 0U; n < 1201U; n++) {
        OpenFOC_ApplyCommand(NULL);
        OpenFOC_Ramp_1ms();
    }
    expect_phase(FOC_PHASE_IDLE);
    foc_aligning = 1U;
    foc_run_request = 1U;
    foc_control_state_request = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    foc_control_state_active = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    /* Getter is read-only and must ignore spoofed public diagnostic mirrors. */
    CHECK(OpenFOC_GetRunPhase() == FOC_PHASE_IDLE);
    CHECK(foc_aligning == 1U);
    OpenFOC_ApplyCommand(NULL); /* Refresh even though private command was idle. */
    expect_phase(FOC_PHASE_IDLE);
    feedback();
    OpenFOC_Run_20kHz();
    neutral();
}

static void run_phase_alignment_boundaries(void)
{
    unsigned int n;
    AxisCommandFrame a = frame(10.0f, 0.5f);
    AxisCommandFrame b = frame(20.0f, 0.25f);
    b.opcode = AXIS_CMD_UPDATE;
    OpenFOC_Init();
    feedback();
    OpenFOC_ApplyCommand(&a);
    expect_phase(FOC_PHASE_ALIGNING); /* Before any slow call. */
    foc_aligning = 0U;
    foc_run_request = 0U;
    CHECK(OpenFOC_GetRunPhase() == FOC_PHASE_ALIGNING);
    OpenFOC_ApplyCommand(&a);
    expect_phase(FOC_PHASE_ALIGNING);
    for (n = 0U; n < 600U; n++) OpenFOC_Ramp_1ms();
    OpenFOC_ApplyCommand(&b);
    expect_phase(FOC_PHASE_ALIGNING);
    CHECK(foc_speed_target_rpm == 20.0f);
    /* Reprojection and UPDATE may change inputs but must not reset the timer. */
    for (n = 0U; n < 50U; n++) OpenFOC_ApplyCommand(&b);
    for (n = 0U; n < 599U; n++) OpenFOC_Ramp_1ms();
    expect_phase(FOC_PHASE_ALIGNING); /* 1199 slow calls in total. */
    OpenFOC_Ramp_1ms();
    expect_phase(FOC_PHASE_CLOSED_LOOP); /* Exact existing 1200-call boundary. */
    foc_aligning = 1U;
    foc_control_state_active = FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST;
    CHECK(OpenFOC_GetRunPhase() == FOC_PHASE_CLOSED_LOOP);
    b.speed_target_rpm = -20.0f;
    OpenFOC_ApplyCommand(&b);
    expect_phase(FOC_PHASE_CLOSED_LOOP);
    CHECK(foc_speed_target_rpm == -20.0f);
    OpenFOC_Stop();
    expect_phase(FOC_PHASE_IDLE);
    neutral();
    OpenFOC_ApplyCommand(&a); /* No slow call between STOP and fresh command. */
    expect_phase(FOC_PHASE_ALIGNING);
    OpenFOC_ApplyCommand(NULL);
    expect_phase(FOC_PHASE_IDLE);
    for (n = 0U; n < 1201U; n++) OpenFOC_Ramp_1ms();
    expect_phase(FOC_PHASE_IDLE);
    neutral();
}

/* Pure CPU adapter of the EXISTING runtime contract: after actual core Tick,
 * project active or NULL. No port, IRQ, physical gate or scheduler is simulated. */
static void project_core(AxisCommandCore *core)
{
    OpenFOC_ApplyCommand(core->active_valid != 0U ? &core->active : NULL);
}

static AxisCommandFrame core_frame(const AxisCommandCore *core, uint32_t now)
{
    AxisCommandFrame f = frame(10.0f, 0.5f);
    f.command_seq = 0U;
    f.run_epoch = core->run_epoch;
    f.received_tick_ms = now;
    f.expires_tick_ms = now + 2000U;
    return f;
}

static void publish_core(AxisCommandCore *core, AxisCommandFrame *f, uint32_t now)
{
    uint32_t seq = 0U;
    CHECK(AxisCommand_Validate(&core->limits, f, now) == AXIS_CMD_OK);
    CHECK(AxisCommand_PublishValidated(core, f, now, 0U, &seq) == AXIS_CMD_OK);
    CHECK(seq != 0U);
}

static void cancelled_phase_and_integrators(void)
{
    expect_phase(FOC_PHASE_IDLE);
    CHECK(foc_run_request == 0U);
    CHECK(foc_speed_ref_rpm == 0.0f);
    CHECK(foc_iq_ref_a == 0.0f);
    CHECK(foc_speed_integrator == 0.0f);
    CHECK(foc_speed_pi_iq_ref_a == 0.0f);
    CHECK(foc_id_integrator == 0.0f);
    CHECK(foc_iq_integrator == 0.0f);
    neutral();
}

static void core_cancellation_projects_idle(void)
{
    static const uint32_t reasons[] = {
        AXIS_CMD_STOPPED, AXIS_CMD_EXPIRED, AXIS_CMD_NOT_READY, AXIS_CMD_FAULT
    };
    /* Synthetic limits only; enough lease to cover the host slow-call loop.
     * No production commissioning values or output permissions are supplied. */
    const AxisCommandLimits limits = {1U, 1000.0f, 4.0f, 5000U};
    unsigned int closed;
    unsigned int action;
    for (closed = 0U; closed < 2U; closed++) {
        for (action = 0U; action < sizeof(reasons) / sizeof(reasons[0]); action++) {
            AxisCommandCore core;
            AxisCommandFrame f;
            uint32_t n;
            uint32_t now;
            uint32_t ticket = 0U;
            uint32_t seq = 0U;
            uint32_t fault;
            uint32_t ready;
            AxisCommand_Init(&core, &limits);
            OpenFOC_Init();
            feedback();
            f = core_frame(&core, 0U);
            publish_core(&core, &f, 0U);
            AxisCommand_Tick(&core, 0U, 0U, 1U);
            project_core(&core);
            expect_phase(FOC_PHASE_ALIGNING);
            if (closed != 0U) {
                for (n = 1U; n <= 1200U; n++) {
                    AxisCommand_Tick(&core, n, 0U, 1U);
                    project_core(&core);
                    OpenFOC_Ramp_1ms();
                }
                expect_phase(FOC_PHASE_CLOSED_LOOP);
            }
            foc_speed_ref_rpm = 3.0f;
            foc_iq_ref_a = 0.4f;
            foc_speed_integrator = 0.4f;
            foc_speed_pi_iq_ref_a = 0.4f;
            foc_id_integrator = 0.4f;
            foc_iq_integrator = 0.4f;
            now = closed != 0U ? 1201U : 1U;
            if (action == 0U) {
                CHECK(AxisCommand_RequestStop(&core, &ticket) == AXIS_CMD_OK);
                CHECK(ticket != 0U);
                /* Task-side request alone does not write FOC private state. */
                CHECK(OpenFOC_GetRunPhase() ==
                      (closed != 0U ? FOC_PHASE_CLOSED_LOOP : FOC_PHASE_ALIGNING));
            } else if (action == 1U) {
                now = f.expires_tick_ms;
            }
            fault = action == 3U ? 1U : 0U;
            ready = action == 2U ? 0U : 1U;
            AxisCommand_Tick(&core, now, fault, ready);
            project_core(&core);
            CHECK(core.result == reasons[action]);
            CHECK(core.active_valid == 0U);
            CHECK(core.run_epoch == 2U);
            cancelled_phase_and_integrators();
            /* Restoring external conditions never resurrects the old command. */
            AxisCommand_Tick(&core, now + 1U, 0U, 1U);
            project_core(&core);
            OpenFOC_Ramp_1ms();
            cancelled_phase_and_integrators();
            f = core_frame(&core, now + 2U);
            if (action == 3U) {
                CHECK(AxisCommand_Validate(&core.limits, &f, now + 2U) == AXIS_CMD_OK);
                CHECK(AxisCommand_PublishValidated(&core, &f, now + 2U, 0U, &seq)
                      == AXIS_CMD_FAULT);
                AxisCommand_Tick(&core, now + 2U, 0U, 1U);
                project_core(&core);
                expect_phase(FOC_PHASE_IDLE); /* Latched fault has no clear API. */
            } else {
                publish_core(&core, &f, now + 2U);
                AxisCommand_Tick(&core, now + 2U, 0U, 1U);
                project_core(&core);
                expect_phase(FOC_PHASE_ALIGNING); /* A fresh legal START. */
            }
        }
    }
}

int main(void)
{
    legacy_writes_without_command();
    legacy_writes_do_not_override_active();
    stop_start_without_slow_tick();
    limit_drop_without_slow_tick();
    run_phase_idle_and_mirror();
    run_phase_alignment_boundaries();
    core_cancellation_projects_idle();
    printf("PASS: %u FOC command integration assertions; CPU register stubs only\n", checks);
    return 0;
}
