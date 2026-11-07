#include "axis_command_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
static unsigned cases;
#define CHECK(x) do { assertions++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CASE(name) do { cases++; puts(name); } while (0)

/* Synthetic HOST fixtures only; these are not board operating limits. */
static const AxisCommandLimits fixture = {1U, 1000.0f, 4.0f, 100U};

static AxisCommandFrame Frame(uint32_t epoch, uint32_t op, uint32_t now)
{
    AxisCommandFrame f;
    f.command_seq = 0U; f.run_epoch = epoch; f.opcode = op;
    f.control_mode = AXIS_CMD_SPEED_MODE; f.speed_target_rpm = 100.0f;
    f.iq_limit_a = 2.0f; f.received_tick_ms = now; f.expires_tick_ms = now + 50U;
    return f;
}

static uint32_t Publish(AxisCommandCore *c, AxisCommandFrame *f, uint32_t now)
{
    uint32_t seq = 0U;
    CHECK(AxisCommand_Validate(&c->limits, f, now) == AXIS_CMD_OK);
    CHECK(AxisCommand_PublishValidated(c, f, now, 0U, &seq) == AXIS_CMD_OK);
    CHECK(seq != 0U);
    return seq;
}

static void Start(AxisCommandCore *c, uint32_t now)
{
    AxisCommandFrame f = Frame(c->run_epoch, AXIS_CMD_START, now);
    Publish(c, &f, now);
    AxisCommand_Tick(c, now, 0U, 1U);
    CHECK(c->result == AXIS_CMD_APPLIED);
    CHECK(AxisCommand_Permitted(c, now, 0U));
}

static float FloatBits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void TestValidation(void)
{
    AxisCommandLimits l = fixture;
    AxisCommandFrame f = Frame(1U, AXIS_CMD_START, 100U);
    AxisCommandCore c;
    uint32_t ticket, seq;
    CASE("validation/configuration/finite values");
    CHECK(sizeof(f) == 32U);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_OK);
    l.confirmed = 0U;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_CONFIG_UNSET);
    AxisCommand_Init(&c, &l);
    CHECK(!c.config_valid && !AxisCommand_Permitted(&c, 100U, 0U));
    CHECK(AxisCommand_PublishValidated(&c, &f, 100U, 0U, &seq) == AXIS_CMD_CONFIG_UNSET);
    CHECK(seq == 0U && c.last_accepted_seq == 0U);
    CHECK(AxisCommand_RequestStop(&c, &ticket) == AXIS_CMD_OK);
    AxisCommand_Tick(&c, 100U, 0U, 1U);
    CHECK(c.stop_applied_ticket == ticket && c.run_epoch == 2U);
    l = fixture; l.max_lifetime_ms = 0x80000000UL;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_CONFIG_UNSET);
    l = fixture; l.max_iq_a = FloatBits(0x7f800000UL);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_CONFIG_UNSET);
    l = fixture;
    f.command_seq = 1U;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_BAD_SEQUENCE);
    f.command_seq = 0U; f.control_mode = 0U;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_UNSUPPORTED_MODE);
    f.control_mode = 2U;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_UNSUPPORTED_MODE);
    f.control_mode = 1U; f.opcode = 99U;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.opcode = AXIS_CMD_START; f.speed_target_rpm = FloatBits(0x7fc00000UL);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.speed_target_rpm = FloatBits(0xff800000UL);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.speed_target_rpm = 1001.0f;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.speed_target_rpm = -1001.0f;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.speed_target_rpm = -1000.0f; f.iq_limit_a = 0.0f;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_OK);
    f.iq_limit_a = -0.1f;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.iq_limit_a = 4.1f;
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.iq_limit_a = FloatBits(0x7fc00000UL);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
    f.iq_limit_a = FloatBits(0x7f800000UL);
    CHECK(AxisCommand_Validate(&l, &f, 100U) == AXIS_CMD_INVALID);
}

static void TestGroupsAndBusy(void)
{
    AxisCommandCore c;
    AxisCommandFrame a, b, saved;
    uint32_t seq, control;
    CASE("whole-frame A/B, full slot and separate acceptance/application");
    AxisCommand_Init(&c, &fixture);
    a = Frame(c.run_epoch, AXIS_CMD_START, 100U);
    Publish(&c, &a, 100U);
    CHECK(!c.active_valid && c.applied_seq == 0U);
    saved = c.pending;
    b = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
    b.speed_target_rpm = -750.0f; b.iq_limit_a = 0.25f;
    CHECK(AxisCommand_PublishValidated(&c, &b, 101U, 0U, &seq) == AXIS_CMD_BUSY);
    CHECK(seq == 0U && c.last_accepted_seq == 1U);
    CHECK(memcmp(&saved, &c.pending, sizeof(saved)) == 0);
    a.speed_target_rpm = 999.0f; /* Caller can reuse private input after publish. */
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(memcmp(&saved, &c.active, sizeof(saved)) == 0);
    CHECK(c.applied_seq == 1U && c.applied_control_seq == c.control_seq);
    control = c.control_seq;
    Publish(&c, &b, 101U);
    CHECK(c.active.speed_target_rpm == 100.0f && c.active.iq_limit_a == 2.0f);
    CHECK(AxisCommand_Guard(&c, 101U, 0U));
    CHECK(c.control_seq == control && c.pending_ready);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.active.speed_target_rpm == -750.0f && c.active.iq_limit_a == 0.25f);
    CHECK(c.applied_seq == 2U && !c.pending_ready);
}

static void TestStop(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    uint32_t t1, t2, seq, epoch;
    CASE("STOP full-slot priority/idempotence/old epoch rejection");
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U); Publish(&c, &f, 101U);
    epoch = c.run_epoch;
    CHECK(AxisCommand_RequestStop(&c, &t1) == AXIS_CMD_OK);
    CHECK(AxisCommand_RequestStop(&c, &t2) == AXIS_CMD_OK && t1 == t2);
    CHECK(AxisCommand_NeedsDisarm(&c, 101U, 0U));
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_STOP_PENDING);
    AxisCommand_Tick(&c, 150U, 0U, 1U); /* Active expiry and STOP: STOP wins. */
    CHECK(c.result == AXIS_CMD_STOPPED && c.run_epoch == epoch + 1U);
    CHECK(!c.active_valid && !c.pending_ready && !c.stop_pending);
    CHECK(c.result_seq == 1U && c.cancelled_seq == 2U);
    CHECK(c.stop_applied_ticket == t1);
    AxisCommand_Tick(&c, 151U, 0U, 1U);
    CHECK(c.run_epoch == epoch + 1U);
    f = Frame(epoch, AXIS_CMD_START, 151U);
    CHECK(AxisCommand_PublishValidated(&c, &f, 151U, 0U, &seq) == AXIS_CMD_STALE_EPOCH);
    Start(&c, 151U); /* A newly captured current epoch plus explicit START works. */
}

static void TestLifecycle(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    CASE("START readiness rejection and UPDATE lifecycle");
    AxisCommand_Init(&c, &fixture);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 100U); Publish(&c, &f, 100U);
    AxisCommand_Tick(&c, 100U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_NOT_RUNNING && !c.active_valid);
    f.opcode = AXIS_CMD_START; Publish(&c, &f, 100U);
    AxisCommand_Tick(&c, 100U, 0U, 0U);
    CHECK(c.result == AXIS_CMD_NOT_READY && !c.pending_ready && !c.active_valid);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(!c.active_valid); /* Readiness becoming true cannot auto-start. */
    Start(&c, 101U);
    f = Frame(c.run_epoch, AXIS_CMD_START, 102U); Publish(&c, &f, 102U);
    AxisCommand_Tick(&c, 102U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_ALREADY_RUNNING && c.active.command_seq == 3U);
    AxisCommand_Revoke(&c, AXIS_CMD_NOT_READY);
    CHECK(!c.active_valid && c.run_epoch == 2U);
    AxisCommand_Revoke(&c, AXIS_CMD_NOT_READY);
    CHECK(c.run_epoch == 2U);
    Start(&c, 103U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 104U); Publish(&c, &f, 104U);
    AxisCommand_Tick(&c, 104U, 0U, 0U);
    CHECK(!c.active_valid && !c.pending_ready && c.run_epoch == 3U);
    CHECK(c.result == AXIS_CMD_NOT_READY);
    AxisCommand_Tick(&c, 105U, 0U, 1U);
    CHECK(!c.active_valid && c.run_epoch == 3U);
}

static void TestTime(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    uint32_t seq;
    CASE("pending/active expiry, no renewal of expired run, tick wrap/ambiguity");
    AxisCommand_Init(&c, &fixture);
    f = Frame(1U, AXIS_CMD_START, 100U);
    CHECK(AxisCommand_Validate(&fixture, &f, 99U) == AXIS_CMD_EXPIRED);
    CHECK(AxisCommand_Validate(&fixture, &f, 149U) == AXIS_CMD_OK);
    CHECK(AxisCommand_Validate(&fixture, &f, 150U) == AXIS_CMD_EXPIRED);
    CHECK(AxisCommand_Validate(&fixture, &f, 100U + 0x80000000UL) == AXIS_CMD_EXPIRED);
    Publish(&c, &f, 100U); AxisCommand_Tick(&c, 150U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_EXPIRED && !c.active_valid && c.run_epoch == 1U);
    Start(&c, 200U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 249U); Publish(&c, &f, 249U);
    CHECK(AxisCommand_NeedsDisarm(&c, 250U, 0U));
    AxisCommand_Tick(&c, 250U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_EXPIRED && !c.active_valid && !c.pending_ready);
    CHECK(c.result_seq == 2U && c.cancelled_seq == 3U);
    CHECK(c.run_epoch == 2U);
    AxisCommand_Tick(&c, 251U, 0U, 1U); CHECK(c.run_epoch == 2U);
    AxisCommand_Init(&c, &fixture);
    f = Frame(1U, AXIS_CMD_START, 0xfffffff0UL);
    CHECK(AxisCommand_Validate(&fixture, &f, 10U) == AXIS_CMD_OK);
    Publish(&c, &f, 0xfffffff0UL); AxisCommand_Tick(&c, 10U, 0U, 1U);
    CHECK(AxisCommand_Permitted(&c, 33U, 0U));
    CHECK(!AxisCommand_Guard(&c, 34U, 0U) && c.result == AXIS_CMD_EXPIRED);
    f = Frame(c.run_epoch, AXIS_CMD_START, 50U); f.expires_tick_ms = 50U;
    CHECK(AxisCommand_Validate(&fixture, &f, 50U) == AXIS_CMD_EXPIRED);
    f.expires_tick_ms = 50U + 0x80000000UL;
    CHECK(AxisCommand_Validate(&fixture, &f, 50U) == AXIS_CMD_EXPIRED);
    f.expires_tick_ms = 151U;
    CHECK(AxisCommand_Validate(&fixture, &f, 50U) == AXIS_CMD_EXPIRED);
    f = Frame(c.run_epoch, AXIS_CMD_START, 50U);
    CHECK(AxisCommand_PublishValidated(&c, &f, 100U, 0U, &seq) == AXIS_CMD_EXPIRED);
}

static void TestFault(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    uint32_t seq, epoch, cycle, ticket;
    CASE("fault after publication and final-gate fault, sticky denial");
    AxisCommand_Init(&c, &fixture);
    f = Frame(1U, AXIS_CMD_START, 100U); Publish(&c, &f, 100U);
    AxisCommand_Tick(&c, 100U, 1U, 1U);
    CHECK(c.result == AXIS_CMD_FAULT && c.fault_latched && !c.active_valid && !c.pending_ready);
    CHECK(c.run_epoch == 2U);
    AxisCommand_Tick(&c, 101U, 1U, 1U); CHECK(c.run_epoch == 2U);
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_FAULT);
    CHECK(!AxisCommand_Permitted(&c, 101U, 0U));
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    cycle = c.control_seq; epoch = c.run_epoch;
    CHECK(AxisCommand_NeedsDisarm(&c, 100U, 1U));
    CHECK(!AxisCommand_Guard(&c, 100U, 1U));
    CHECK(!c.active_valid && c.control_seq == cycle && c.run_epoch == epoch + 1U);
    AxisCommand_RequestStop(&c, &ticket);
    AxisCommand_Tick(&c, 101U, 1U, 1U);
    CHECK(c.stop_applied_ticket == ticket && c.run_epoch == epoch + 1U);
}

static void TestSequences(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    uint32_t seq, ticket;
    CASE("bad sequence/epoch and all metadata exhaustion paths");
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    f = Frame(1U, AXIS_CMD_UPDATE, 101U); Publish(&c, &f, 101U);
    c.pending.command_seq = 1U;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_BAD_SEQUENCE && c.active.command_seq == 1U);
    Publish(&c, &f, 101U); c.pending.command_seq = 0U;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_BAD_SEQUENCE);
    Publish(&c, &f, 101U); c.pending.run_epoch = 2U;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_STALE_EPOCH);
    c.active.run_epoch = 2U;
    CHECK(!AxisCommand_Guard(&c, 101U, 0U) && !c.active_valid);
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    c.last_accepted_seq = 0xffffffffUL;
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_EXHAUSTED);
    CHECK(seq == 0U && c.exhausted && AxisCommand_NeedsDisarm(&c, 101U, 0U));
    AxisCommand_Tick(&c, 101U, 0U, 1U); CHECK(!c.active_valid);
    AxisCommand_Init(&c, &fixture); c.run_epoch = 0xfffffffeUL;
    Start(&c, 100U); AxisCommand_RequestStop(&c, &ticket);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.exhausted && !c.active_valid && c.run_epoch == 0xffffffffUL);
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    c.control_seq = 0xffffffffUL;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(!c.exhausted && c.active_valid && c.control_seq == 0U);
    CHECK(AxisCommand_Permitted(&c, 101U, 0U));
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    c.stop_ticket = 0xffffffffUL;
    AxisCommand_RequestStop(&c, &ticket);
    CHECK(ticket == 0xffffffffUL && c.stop_pending);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.exhausted && !c.active_valid && c.stop_applied_ticket == ticket);
}

static void TestIq(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    float iq = 2.0f, pi = -1.5f, integrator = 1.25f;
    CASE("limit decrease on non-slow-loop tick; invalid clamp inputs");
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U); f.iq_limit_a = 0.125f;
    Publish(&c, &f, 101U); AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK((c.control_seq % 20U) != 0U);
    iq = AxisCommand_ClampIq(iq, c.active.iq_limit_a);
    pi = AxisCommand_ClampIq(pi, c.active.iq_limit_a);
    integrator = AxisCommand_ClampIq(integrator, c.active.iq_limit_a);
    CHECK(iq == 0.125f && pi == -0.125f && integrator == 0.125f);
    CHECK(AxisCommand_ClampIq(2.0f, 0.0f) == 0.0f);
    CHECK(AxisCommand_ClampIq(0.1f, -1.0f) == 0.0f);
    CHECK(AxisCommand_ClampIq(FloatBits(0x7f800000UL), 1.0f) == 0.0f);
    CHECK(AxisCommand_ClampIq(FloatBits(0x7fc00000UL), 1.0f) == 0.0f);
    CHECK(AxisCommand_ClampIq(1.0f, FloatBits(0x7fc00000UL)) == 0.0f);
}

int main(void)
{
    TestValidation(); TestGroupsAndBusy(); TestStop(); TestLifecycle();
    TestTime(); TestFault(); TestSequences(); TestIq();
    printf("PASS: %u scenarios, %u assertions; synthetic HOST limits only.\n", cases, assertions);
    puts("Boundary: sequential core tests do not prove Cortex-M7 PRIMASK timing or physical PWM safety.");
    return 0;
}
