#include "axis_command_bench.h"
#include "axis_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same sequencer and command core as target. These port stubs model sequential
 * task/ISR scheduling and corrupted/lost results; they do not emulate PRIMASK,
 * STM32 registers, real time, physical output isolation or true preemption. */
static AxisCommandCore core;
static const AxisCommandLimits fixture = {1U, 1000.0f, 4.0f, 1000U};
static uint32_t now_ms, ready, output_safe, fault, overruns;
static uint32_t freeze_tick, fail_snapshot, hide_apply_ack, hide_stop_ack;
static uint32_t mix_frame, force_active, submit_calls, stop_calls;
static uint32_t consume_inside_submit;
static unsigned checks, cases;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr,"FAIL %s:%d %s (stage=%lu mask=%lu error=%lu)\n", \
        __FILE__,__LINE__,#x,(unsigned long)axis_bench_stage, \
        (unsigned long)axis_bench_pass_mask,(unsigned long)axis_bench_fail_code); \
    exit(1); } } while (0)
#define CASE(s) do { cases++; puts(s); } while (0)

uint32_t AxisCommandPort_Submit(const AxisCommandFrame *frame, uint32_t *seq)
{
    uint32_t result;
    submit_calls++;
    *seq = 0U;
    result = AxisCommand_Validate(&fixture, frame, now_ms);
    if (result != AXIS_CMD_OK) return result;
    result = AxisCommand_PublishValidated(&core, frame, now_ms, fault, seq);
    /* IRQ can run after PRIMASK restore but before Submit returns to caller. */
    if (consume_inside_submit) AxisCommand_Tick(&core, now_ms, fault, ready);
    return result;
}

uint32_t AxisCommandPort_Stop(uint32_t *ticket)
{
    stop_calls++;
    return AxisCommand_RequestStop(&core, ticket);
}

uint32_t AxisCommandPort_GetSnapshot(AxisCommandSnapshot *s)
{
    if (fail_snapshot) return AXIS_CMD_INVALID;
    s->active = core.active;
    s->active_valid = core.active_valid;
    s->run_epoch = core.run_epoch;
    s->applied_seq = core.applied_seq;
    s->applied_control_seq = core.applied_control_seq;
    s->control_seq = core.control_seq;
    s->result_seq = core.result_seq;
    s->result = core.result;
    s->cancelled_seq = core.cancelled_seq;
    s->stop_applied_ticket = core.stop_applied_ticket;
    s->permit = AxisCommand_Permitted(&core, now_ms, fault);
    if (hide_apply_ack)
    {
        s->active_valid = 0U; s->permit = 0U; s->applied_seq = 0U;
        s->result = AXIS_CMD_OK;
    }
    if (hide_stop_ack) s->stop_applied_ticket = 0U;
    if (mix_frame == 1U) s->active.iq_limit_a = 1.0f; /* B target + A limit. */
    if (mix_frame == 2U) s->active.speed_target_rpm = -200.0f; /* A seq + B target. */
    if (force_active) { s->active_valid = 1U; s->permit = 1U; }
    return AXIS_CMD_OK;
}

static void Init(uint32_t start)
{
    AxisCommand_Init(&core, &fixture);
    now_ms = start; ready = 1U; output_safe = 1U; fault = 0U; overruns = 0U;
    freeze_tick = fail_snapshot = hide_apply_ack = hide_stop_ack = 0U;
    mix_frame = force_active = submit_calls = stop_calls = 0U;
    consume_inside_submit = 0U;
    AxisCommandBench_Init(now_ms);
}

static void TimTicks(void)
{
    unsigned i;
    if (freeze_tick) return;
    for (i = 0; i < 20U; i++) AxisCommand_Tick(&core, now_ms, fault, ready);
}

static void Step(void)
{
    now_ms++;
    TimTicks();
    AxisCommandBench_Step(now_ms, ready, output_safe, fault, overruns);
}

static void Until(uint32_t desired, unsigned limit)
{
    while (axis_bench_stage != desired && limit-- != 0U) Step();
    CHECK(axis_bench_stage == desired);
}

static void CheckFailed(uint32_t error)
{
    uint32_t stops = stop_calls, submissions = submit_calls;
    unsigned i;
    CHECK(axis_bench_stage == AXIS_BENCH_FAIL);
    CHECK(axis_bench_fail_code == error);
    CHECK(stops != 0U);
    /* Fail remains sticky even if the injected condition is removed. */
    ready = output_safe = 1U; fault = overruns = freeze_tick = 0U;
    fail_snapshot = hide_apply_ack = hide_stop_ack = mix_frame = force_active = 0U;
    for (i = 0; i < 30U; i++) Step();
    CHECK(axis_bench_stage == AXIS_BENCH_FAIL && axis_bench_fail_code == error);
    CHECK(stop_calls == stops && submit_calls == submissions);
    CHECK(!core.active_valid && !core.pending_ready);
}

static void TestNormal(uint32_t start, uint32_t inline_irq)
{
    uint32_t calls, stops;
    unsigned i;
    CASE(inline_irq ? "complete round: consume before Submit returns" :
                     "complete round: async consumer / HAL and control counter wrap");
    Init(start);
    consume_inside_submit = inline_irq;
    core.control_seq = 0xfffffff0UL;
    Until(AXIS_BENCH_PASS, 500U);
    CHECK(axis_bench_pass_mask == 0x3fU && axis_bench_fail_code == 0U);
    CHECK(core.run_epoch == 3U && core.last_accepted_seq == 3U);
    CHECK(!core.active_valid && !core.pending_ready && !core.stop_pending);
    CHECK(submit_calls == 4U && stop_calls == 1U);
    calls = submit_calls; stops = stop_calls;
    for (i = 0; i < 500U; i++) Step();
    CHECK(axis_bench_stage == AXIS_BENCH_PASS);
    CHECK(calls == submit_calls && stops == stop_calls); /* One round only. */
    force_active = 1U; Step(); CheckFailed(AXIS_BENCH_ERR_RESTART);
}

static void TestReady(void)
{
    unsigned i;
    CASE("startup stable-ready delay; startup timeout; readiness loss");
    Init(0U); ready = 0U;
    for (i = 0; i < 100U; i++) Step();
    CHECK(axis_bench_stage == AXIS_BENCH_WAIT_READY && submit_calls == 0U);
    ready = 1U;
    for (i = 0; i < 10U; i++) Step();
    ready = 0U; Step(); ready = 1U;
    for (i = 0; i < 20U; i++) Step();
    CHECK(axis_bench_stage == AXIS_BENCH_WAIT_READY);
    Step(); CHECK(axis_bench_stage == AXIS_BENCH_WAIT_A);
    ready = 0U; Step(); CheckFailed(AXIS_BENCH_ERR_READY_LOST);
    Init(0xfffffff0UL); ready = 0U;
    Until(AXIS_BENCH_FAIL, 5001U); CheckFailed(AXIS_BENCH_ERR_READY_TIMEOUT);
}

static void TestSafetyInputs(void)
{
    CASE("unsafe output, fault, ISR overrun, failed snapshot each latch FAIL");
    Init(0U); output_safe = 0U; Step(); CheckFailed(AXIS_BENCH_ERR_OUTPUT);
    Init(0U); Until(AXIS_BENCH_WAIT_B, 40U);
    fault = 1U; Step(); CheckFailed(AXIS_BENCH_ERR_FAULT);
    Init(0U); overruns = 1U; Step(); CheckFailed(AXIS_BENCH_ERR_OVERRUN);
    Init(0U); fail_snapshot = 1U; Step(); CheckFailed(AXIS_BENCH_ERR_SNAPSHOT);
}

static void TestLiveness(void)
{
    unsigned i;
    CASE("TIM1 stall before apply and after expiry cannot falsely PASS");
    Init(0U); Until(AXIS_BENCH_WAIT_A, 30U);
    freeze_tick = 1U; Until(AXIS_BENCH_FAIL, 25U);
    CheckFailed(AXIS_BENCH_ERR_TIM1_STALLED);
    Init(0U); Until(AXIS_BENCH_WAIT_IDLE, 150U);
    freeze_tick = 1U; Until(AXIS_BENCH_FAIL, 25U);
    CheckFailed(AXIS_BENCH_ERR_TIM1_STALLED);
    Init(0U); Until(AXIS_BENCH_WAIT_A, 30U);
    freeze_tick = 1U;
    for (i = 0; i < 10U; i++) Step();
    CHECK(axis_bench_stage == AXIS_BENCH_WAIT_A);
    freeze_tick = 0U; Until(AXIS_BENCH_PASS, 500U);
    CHECK(axis_bench_pass_mask == 0x3fU); /* Bounded delayed consumer succeeds. */
    Init(0U);
    for (i = 0; i < 500U; i++)
    {
        TimTicks(); AxisCommandBench_Step(now_ms, ready, output_safe, fault, overruns);
    }
    CHECK(axis_bench_stage == AXIS_BENCH_WAIT_READY && submit_calls == 0U);
    /* Frozen HAL cannot advance the proof-of-time stages into false success. */
}

static void TestBadAcknowledgements(void)
{
    CASE("lost application/STOP acknowledgement; mixed old/new frame detection");
    Init(0U); Until(AXIS_BENCH_WAIT_A, 30U);
    hide_apply_ack = 1U; Until(AXIS_BENCH_FAIL, 210U);
    CheckFailed(AXIS_BENCH_ERR_ACK_TIMEOUT);
    Init(0U); Until(AXIS_BENCH_WAIT_STOP, 40U);
    hide_stop_ack = 1U; Until(AXIS_BENCH_FAIL, 210U);
    CheckFailed(AXIS_BENCH_ERR_ACK_TIMEOUT);
    Init(0U); Until(AXIS_BENCH_WAIT_B, 40U);
    mix_frame = 1U; Step(); CheckFailed(AXIS_BENCH_ERR_FRAME);
    Init(0U); Until(AXIS_BENCH_WAIT_B, 40U);
    freeze_tick = 1U; mix_frame = 2U; Step(); CheckFailed(AXIS_BENCH_ERR_FRAME);
}

static void TestLateObservation(void)
{
    unsigned i;
    CASE("late task must observe START application before claiming expiry success");
    Init(0U); Until(AXIS_BENCH_WAIT_EXPIRING_START, 50U);
    for (i = 0; i < 60U; i++) { now_ms++; TimTicks(); }
    AxisCommandBench_Step(now_ms, ready, output_safe, fault, overruns);
    CheckFailed(AXIS_BENCH_ERR_REJECTED);
    CHECK((axis_bench_pass_mask & 0x10U) == 0U);
}

int main(void)
{
    TestNormal(0U, 0U); TestNormal(0xfffffff0UL, 0U); TestNormal(1000U, 1U);
    TestReady(); TestSafetyInputs(); TestLiveness(); TestBadAcknowledgements();
    TestLateObservation();
    printf("PASS: %u bench sequencer groups / %u assertions; real command core, simulated port.\n",cases,checks);
    puts("No assertion proves hardware inhibition, IRQ timing or physical current safety.");
    return 0;
}
