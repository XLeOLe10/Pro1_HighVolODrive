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

static void CheckReceipt(const AxisCommandCore *c, uint32_t seq,
                         uint32_t result, uint32_t cycle)
{
    CHECK(c->receipt.command_seq == seq);
    CHECK(c->receipt.result == result);
    CHECK(c->receipt.control_seq == cycle);
}

static void TestValidation(void)
{
    AxisCommandLimits l = fixture;
    AxisCommandFrame f = Frame(1U, AXIS_CMD_START, 100U);
    AxisCommandCore c;
    uint32_t ticket, seq;
    CASE("validation/configuration/finite values");
    CHECK(sizeof(f) == 32U);
    CHECK(sizeof(AxisCommandReceipt) == 12U);
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
    CheckReceipt(&c, 2U, AXIS_CMD_STOPPED, c.control_seq);
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
    CheckReceipt(&c, 1U, AXIS_CMD_NOT_RUNNING, c.control_seq);
    f.opcode = AXIS_CMD_START; Publish(&c, &f, 100U);
    AxisCommand_Tick(&c, 100U, 0U, 0U);
    CHECK(c.result == AXIS_CMD_NOT_READY && !c.pending_ready && !c.active_valid);
    CheckReceipt(&c, 2U, AXIS_CMD_NOT_READY, c.control_seq);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(!c.active_valid); /* Readiness becoming true cannot auto-start. */
    Start(&c, 101U);
    f = Frame(c.run_epoch, AXIS_CMD_START, 102U); Publish(&c, &f, 102U);
    AxisCommand_Tick(&c, 102U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_ALREADY_RUNNING && c.active.command_seq == 3U);
    CheckReceipt(&c, 4U, AXIS_CMD_ALREADY_RUNNING, c.control_seq);
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
    CheckReceipt(&c, 1U, AXIS_CMD_EXPIRED, c.control_seq);
    Start(&c, 200U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 249U); Publish(&c, &f, 249U);
    CHECK(AxisCommand_NeedsDisarm(&c, 250U, 0U));
    AxisCommand_Tick(&c, 250U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_EXPIRED && !c.active_valid && !c.pending_ready);
    CHECK(c.result_seq == 2U && c.cancelled_seq == 3U);
    CheckReceipt(&c, 3U, AXIS_CMD_EXPIRED, c.control_seq);
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
    CheckReceipt(&c, 2U, AXIS_CMD_BAD_SEQUENCE, c.control_seq);
    Publish(&c, &f, 101U); c.pending.command_seq = 0U;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_BAD_SEQUENCE);
    CheckReceipt(&c, 3U, AXIS_CMD_BAD_SEQUENCE, c.control_seq);
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

static void TestReceiptRetention(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    AxisCommandReceipt saved;
    uint32_t seq, ticket;
    CASE("receipt is installation history; acceptance/rejection/empty STOP do not erase it");
    AxisCommand_Init(&c, &fixture);
    CheckReceipt(&c, 0U, AXIS_CMD_OK, 0U);
    AxisCommand_RequestStop(&c, &ticket);
    AxisCommand_Tick(&c, 100U, 0U, 1U);
    CheckReceipt(&c, 0U, AXIS_CMD_OK, 0U);
    Start(&c, 100U);
    CheckReceipt(&c, 1U, AXIS_CMD_APPLIED, c.applied_control_seq);
    saved = c.receipt;
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
    CHECK(Publish(&c, &f, 101U) == 2U);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_BUSY);
    CHECK(seq == 0U && c.result_seq == 0U);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    CHECK(AxisCommand_ValidateTargets(&fixture, 1.0f, 1.0f) == AXIS_CMD_OK);
    CHECK(AxisCommand_Permitted(&c, 101U, 0U));
    CHECK(AxisCommand_Guard(&c, 101U, 0U));
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CheckReceipt(&c, 2U, AXIS_CMD_APPLIED, c.applied_control_seq);
    saved = c.receipt; /* One slot: the preceding receipt is now replaced. */
    AxisCommand_Revoke(&c, AXIS_CMD_NOT_READY);
    CHECK(!c.active_valid && c.result == AXIS_CMD_NOT_READY);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_STALE_EPOCH);
    CHECK(seq == 0U && c.result_seq == 0U);
    AxisCommand_RequestStop(&c, &ticket);
    AxisCommand_Tick(&c, 102U, 0U, 1U);
    CHECK(c.result_seq == 0U && c.result == AXIS_CMD_STOPPED);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    CHECK(!AxisCommand_Guard(&c, 102U, 1U));
    CHECK(AxisCommand_PublishValidated(&c, &f, 102U, 0U, &seq) == AXIS_CMD_FAULT);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    AxisCommand_Init(&c, &fixture);
    CheckReceipt(&c, 0U, AXIS_CMD_OK, 0U);
}

static void TestReceiptPendingRejections(void)
{
    static const uint32_t expected[] = {
        AXIS_CMD_BAD_SEQUENCE, AXIS_CMD_BAD_SEQUENCE, AXIS_CMD_BAD_SEQUENCE,
        AXIS_CMD_STALE_EPOCH, AXIS_CMD_UNSUPPORTED_MODE, AXIS_CMD_INVALID
    };
    AxisCommandCore c;
    AxisCommandFrame f;
    unsigned i;
    CASE("pending failures are attributed to their allocated sequence, not a corrupt frame sequence");
    for (i = 0U; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        AxisCommand_Init(&c, &fixture); Start(&c, 100U);
        f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
        CHECK(Publish(&c, &f, 101U) == 2U);
        switch (i)
        {
            case 0U: c.pending.command_seq = 0U; break;
            case 1U: c.pending.command_seq = 1U; break;
            case 2U: c.pending.command_seq = 0xffffffffUL; break;
            case 3U: c.pending.run_epoch++; break;
            case 4U: c.pending.control_mode = 99U; break;
            default: c.pending.opcode = 99U; break;
        }
        AxisCommand_Tick(&c, 101U, 0U, 1U);
        CheckReceipt(&c, 2U, expected[i], 2U);
        CHECK(c.result == expected[i] && !c.pending_ready);
        CHECK(c.active_valid && c.active.command_seq == 1U && c.applied_seq == 1U);
        CHECK(c.result_seq == c.pending.command_seq); /* Legacy diagnostic unchanged. */
    }
}

static void TestReceiptRevocations(void)
{
    static const uint32_t reasons[] = {
        AXIS_CMD_FAULT, AXIS_CMD_EXHAUSTED, AXIS_CMD_CONFIG_UNSET,
        AXIS_CMD_STALE_EPOCH, AXIS_CMD_EXPIRED, AXIS_CMD_NOT_READY, AXIS_CMD_STOPPED
    };
    AxisCommandCore c;
    AxisCommandFrame f;
    AxisCommandReceipt saved;
    uint32_t ticket, now, fault, ready;
    unsigned i, with_pending;
    CASE("all revocations retain active history, or replace it with the newer pending cancellation");
    for (with_pending = 0U; with_pending <= 1U; with_pending++)
    {
        for (i = 0U; i < sizeof(reasons) / sizeof(reasons[0]); i++)
        {
            AxisCommand_Init(&c, &fixture); Start(&c, 100U);
            saved = c.receipt;
            if (with_pending)
            {
                f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
                CHECK(Publish(&c, &f, 101U) == 2U);
            }
            now = 101U; fault = 0U; ready = 1U;
            switch (reasons[i])
            {
                case AXIS_CMD_FAULT: fault = 1U; break;
                case AXIS_CMD_EXHAUSTED: c.exhausted = 1U; break;
                case AXIS_CMD_CONFIG_UNSET: c.config_valid = 0U; break;
                case AXIS_CMD_STALE_EPOCH: c.active.run_epoch++; break;
                case AXIS_CMD_EXPIRED: now = 150U; break;
                case AXIS_CMD_NOT_READY: ready = 0U; break;
                default: AxisCommand_RequestStop(&c, &ticket); break;
            }
            AxisCommand_Tick(&c, now, fault, ready);
            CHECK(!c.active_valid && !c.pending_ready && c.result == reasons[i]);
            CHECK(c.result_seq == 1U && c.applied_seq == 1U);
            if (with_pending)
            {
                CheckReceipt(&c, 2U, reasons[i], 2U);
                CHECK(c.cancelled_seq == 2U);
            }
            else CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
            saved = c.receipt;
            AxisCommand_RequestStop(&c, &ticket);
            AxisCommand_Tick(&c, now, fault, ready);
            CHECK(c.stop_applied_ticket == ticket);
            CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
        }
    }
}

static void TestReceiptFaultStopAndFinalGuard(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    AxisCommandReceipt saved;
    uint32_t ticket, seq, cycle;
    unsigned with_active;
    CASE("fault outranks simultaneous STOP for pending receipt; final guard does not invent a new cycle");
    for (with_active = 0U; with_active <= 1U; with_active++)
    {
        AxisCommand_Init(&c, &fixture);
        if (with_active) Start(&c, 100U);
        f = Frame(c.run_epoch, with_active ? AXIS_CMD_UPDATE : AXIS_CMD_START, 101U);
        seq = Publish(&c, &f, 101U);
        AxisCommand_RequestStop(&c, &ticket);
        AxisCommand_Tick(&c, 101U, 1U, 1U);
        CheckReceipt(&c, seq, AXIS_CMD_FAULT, with_active + 1U);
        CHECK(c.result == AXIS_CMD_STOPPED && c.result_seq == 0U);
        CHECK(c.cancelled_seq == seq && c.stop_applied_ticket == ticket);
        saved = c.receipt;
        AxisCommand_Tick(&c, 102U, 1U, 1U);
        CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    }
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
    seq = Publish(&c, &f, 101U); cycle = c.control_seq;
    CHECK(!AxisCommand_Guard(&c, 101U, 1U));
    CheckReceipt(&c, seq, AXIS_CMD_FAULT, cycle);
    CHECK(c.control_seq == cycle && c.applied_seq == 1U);
}

static void TestReceiptSequenceBounds(void)
{
    AxisCommandCore c;
    AxisCommandFrame f;
    AxisCommandReceipt saved;
    uint32_t seq;
    CASE("receipt sequence is nonzero/nonwrapping; control observation may wrap");
    AxisCommand_Init(&c, &fixture); Start(&c, 100U);
    f = Frame(c.run_epoch, AXIS_CMD_UPDATE, 101U);
    CHECK(Publish(&c, &f, 101U) == 2U);
    c.control_seq = 0xffffffffUL;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CheckReceipt(&c, 2U, AXIS_CMD_APPLIED, 0U);
    saved = c.receipt;
    /* Fault-injected reappearing old mailbox must not rewrite its receipt. */
    c.pending_ready = 1U; c.pending.command_seq = 0U;
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_BAD_SEQUENCE);
    CHECK(memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    c.last_accepted_seq = 0xfffffffeUL;
    CHECK(Publish(&c, &f, 101U) == 0xffffffffUL);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CheckReceipt(&c, 0xffffffffUL, AXIS_CMD_APPLIED, 2U);
    saved = c.receipt;
    CHECK(AxisCommand_PublishValidated(&c, &f, 101U, 0U, &seq) == AXIS_CMD_EXHAUSTED);
    CHECK(seq == 0U && c.exhausted);
    AxisCommand_Tick(&c, 101U, 0U, 1U);
    CHECK(!c.active_valid && memcmp(&c.receipt, &saved, sizeof(saved)) == 0);
    AxisCommand_Init(&c, &fixture);
    /* No accepted allocation: do not manufacture a nonzero receipt. */
    c.pending = Frame(c.run_epoch, AXIS_CMD_START, 100U); c.pending_ready = 1U;
    AxisCommand_Tick(&c, 100U, 0U, 1U);
    CHECK(c.result == AXIS_CMD_BAD_SEQUENCE);
    CheckReceipt(&c, 0U, AXIS_CMD_OK, 0U);
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
    TestReceiptRetention(); TestReceiptPendingRejections(); TestReceiptRevocations();
    TestReceiptFaultStopAndFinalGuard(); TestReceiptSequenceBounds();
    printf("PASS: %u scenarios, %u assertions; synthetic HOST limits only.\n", cases, assertions);
    puts("Boundary: sequential core tests do not prove Cortex-M7 PRIMASK timing or physical PWM safety.");
    return 0;
}
