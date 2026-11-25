#include "axis_command_bench.h"
#include "board_config.h"
#include "axis_command.h"

#if AXIS_COMMAND_BENCH_ENABLE

#define READY_STABLE_MS 20U
#define READY_TIMEOUT_MS 5000U
#define ACK_TIMEOUT_MS 200U
#define TIM1_STALL_MS 20U
#define IDLE_OBSERVE_MS 100U
#define COMMAND_LEASE_MS 500U
#define EXPIRY_LEASE_MS 50U

volatile uint32_t axis_bench_stage;
volatile uint32_t axis_bench_pass_mask;
volatile uint32_t axis_bench_fail_code;

static uint32_t stage;
static uint32_t started_ms;
static uint32_t stage_started_ms;
static uint32_t ready_since_ms;
static uint32_t ready_tracking;
static uint32_t expected_seq;
static uint32_t stop_ticket;
static uint32_t old_epoch;
static uint32_t idle_epoch;
static uint32_t last_control_seq;
static uint32_t last_control_progress_ms;
static AxisCommandFrame expected_frame;
static AxisCommandFrame previous_frame;

static void Enter(uint32_t next, uint32_t now)
{
    stage = next;
    stage_started_ms = now;
    axis_bench_stage = next;
}

static void Fail(uint32_t code, uint32_t now)
{
    uint32_t ignored_ticket;
    if (stage == AXIS_BENCH_FAIL) return;
    axis_bench_fail_code = code;
    Enter(AXIS_BENCH_FAIL, now);
    /* Best-effort independent STOP, once. Hardware inhibition does not depend
     * on the task reaching here, this request succeeding, or TIM1 running. */
    (void)AxisCommandPort_Stop(&ignored_ticket);
}

static AxisCommandFrame MakeFrame(uint32_t epoch, uint32_t opcode,
                                  uint32_t now, uint32_t lease,
                                  float speed, float iq)
{
    AxisCommandFrame f;
    f.command_seq = 0U;
    f.run_epoch = epoch;
    f.opcode = opcode;
    f.control_mode = AXIS_CMD_SPEED_MODE;
    f.speed_target_rpm = speed;
    f.iq_limit_a = iq;
    f.received_tick_ms = now;
    f.expires_tick_ms = now + lease;
    return f;
}

static uint32_t SameFrame(const AxisCommandFrame *a, const AxisCommandFrame *b)
{
    return a->command_seq == b->command_seq && a->run_epoch == b->run_epoch &&
        a->opcode == b->opcode && a->control_mode == b->control_mode &&
        a->speed_target_rpm == b->speed_target_rpm && a->iq_limit_a == b->iq_limit_a &&
        a->received_tick_ms == b->received_tick_ms &&
        a->expires_tick_ms == b->expires_tick_ms;
}

static uint32_t Submit(uint32_t next, uint32_t epoch, uint32_t opcode,
                       uint32_t now, uint32_t lease, float speed, float iq)
{
    AxisCommandFrame f = MakeFrame(epoch, opcode, now, lease, speed, iq);
    uint32_t seq = 0U;
    uint32_t result = AxisCommandPort_Submit(&f, &seq);
    if ((result != AXIS_CMD_OK) || (seq == 0U))
    {
        Fail(AXIS_BENCH_ERR_SUBMIT, now);
        return 0U;
    }
    /* Local ownership after Submit: TIM1 may already have consumed the copy.
     * Keep the exact full request for later comparison through one snapshot. */
    f.command_seq = seq;
    expected_frame = f;
    expected_seq = seq;
    Enter(next, now);
    return 1U;
}

/* 1=matching application, 0=wait or latched failure. No scalar Watch reads. */
static uint32_t AwaitApplied(const AxisCommandSnapshot *s, uint32_t now)
{
    if ((s->applied_seq == expected_seq) && s->active_valid)
    {
        if (!s->permit || s->run_epoch != expected_frame.run_epoch ||
            !SameFrame(&s->active, &expected_frame) ||
            s->result_seq != expected_seq || s->result != AXIS_CMD_APPLIED)
            Fail(AXIS_BENCH_ERR_FRAME, now);
        else return 1U;
    }
    else if (s->active_valid &&
             ((stage != AXIS_BENCH_WAIT_B) ||
              !SameFrame(&s->active, &previous_frame)))
        Fail(AXIS_BENCH_ERR_FRAME, now);
    else if ((s->result_seq == expected_seq) && s->result != AXIS_CMD_OK &&
             s->result != AXIS_CMD_APPLIED)
        Fail(AXIS_BENCH_ERR_REJECTED, now);
    else if ((uint32_t)(now - stage_started_ms) >= ACK_TIMEOUT_MS)
        Fail(AXIS_BENCH_ERR_ACK_TIMEOUT, now);
    return 0U;
}

void AxisCommandBench_Init(uint32_t now_ms)
{
    stage = AXIS_BENCH_WAIT_READY;
    started_ms = now_ms;
    stage_started_ms = now_ms;
    ready_since_ms = now_ms;
    ready_tracking = 0U;
    expected_seq = 0U;
    stop_ticket = 0U;
    old_epoch = 0U;
    idle_epoch = 0U;
    last_control_seq = 0U;
    last_control_progress_ms = now_ms;
    axis_bench_stage = stage;
    axis_bench_pass_mask = 0U;
    axis_bench_fail_code = AXIS_BENCH_ERR_NONE;
}

void AxisCommandBench_Step(uint32_t now, uint32_t ready,
                           uint32_t output_safe, uint32_t fault_mask,
                           uint32_t overruns)
{
    AxisCommandSnapshot s;
    if (stage == AXIS_BENCH_FAIL) return;
    if (!output_safe) { Fail(AXIS_BENCH_ERR_OUTPUT, now); return; }
    if (fault_mask) { Fail(AXIS_BENCH_ERR_FAULT, now); return; }
    if (overruns) { Fail(AXIS_BENCH_ERR_OVERRUN, now); return; }
    if (AxisCommandPort_GetSnapshot(&s) != AXIS_CMD_OK)
    { Fail(AXIS_BENCH_ERR_SNAPSHOT, now); return; }

    if (stage != AXIS_BENCH_WAIT_READY)
    {
        if (!ready) { Fail(AXIS_BENCH_ERR_READY_LOST, now); return; }
        if (s.control_seq != last_control_seq)
        {
            last_control_seq = s.control_seq;
            last_control_progress_ms = now;
        }
        else if ((uint32_t)(now - last_control_progress_ms) >= TIM1_STALL_MS)
        { Fail(AXIS_BENCH_ERR_TIM1_STALLED, now); return; }
    }

    switch (stage)
    {
    case AXIS_BENCH_WAIT_READY:
        if (s.active_valid || s.permit)
        { Fail(AXIS_BENCH_ERR_RESTART, now); break; }
        if ((uint32_t)(now - started_ms) >= READY_TIMEOUT_MS)
        { Fail(AXIS_BENCH_ERR_READY_TIMEOUT, now); break; }
        if (!ready) ready_tracking = 0U;
        else if (!ready_tracking)
        { ready_since_ms = now; ready_tracking = 1U; }
        else if ((uint32_t)(now - ready_since_ms) >= READY_STABLE_MS)
        {
            last_control_seq = s.control_seq;
            last_control_progress_ms = now;
            old_epoch = s.run_epoch;
            (void)Submit(AXIS_BENCH_WAIT_A, s.run_epoch, AXIS_CMD_START,
                         now, COMMAND_LEASE_MS, 100.0f, 1.0f);
        }
        break;
    case AXIS_BENCH_WAIT_A:
        if (AwaitApplied(&s, now))
        {
            axis_bench_pass_mask |= 0x01U;
            previous_frame = expected_frame;
            (void)Submit(AXIS_BENCH_WAIT_B, s.run_epoch, AXIS_CMD_UPDATE,
                         now, COMMAND_LEASE_MS, -200.0f, 0.5f);
        }
        break;
    case AXIS_BENCH_WAIT_B:
        if (AwaitApplied(&s, now))
        {
            axis_bench_pass_mask |= 0x02U;
            if (AxisCommandPort_Stop(&stop_ticket) != AXIS_CMD_OK || !stop_ticket)
                Fail(AXIS_BENCH_ERR_STOP, now);
            else Enter(AXIS_BENCH_WAIT_STOP, now);
        }
        break;
    case AXIS_BENCH_WAIT_STOP:
        if (s.stop_applied_ticket == stop_ticket)
        {
            if (s.active_valid || s.permit || s.run_epoch != old_epoch + 1U ||
                s.result_seq != expected_seq || s.result != AXIS_CMD_STOPPED)
                Fail(AXIS_BENCH_ERR_STOP, now);
            else
            {
                axis_bench_pass_mask |= 0x04U;
                Enter(AXIS_BENCH_REJECT_OLD, now);
            }
        }
        else if ((uint32_t)(now - stage_started_ms) >= ACK_TIMEOUT_MS)
            Fail(AXIS_BENCH_ERR_ACK_TIMEOUT, now);
        break;
    case AXIS_BENCH_REJECT_OLD:
        {
            AxisCommandFrame old = MakeFrame(old_epoch, AXIS_CMD_START, now,
                                             COMMAND_LEASE_MS, 100.0f, 1.0f);
            uint32_t seq = 0U;
            uint32_t result;
            if (s.active_valid || s.permit || s.run_epoch != old_epoch + 1U)
            { Fail(AXIS_BENCH_ERR_OLD_EPOCH, now); break; }
            result = AxisCommandPort_Submit(&old, &seq);
            if (result != AXIS_CMD_STALE_EPOCH || seq != 0U)
                Fail(AXIS_BENCH_ERR_OLD_EPOCH, now);
            else
            {
                axis_bench_pass_mask |= 0x08U;
                (void)Submit(AXIS_BENCH_WAIT_EXPIRING_START, s.run_epoch,
                    AXIS_CMD_START, now, EXPIRY_LEASE_MS, 125.0f, 0.25f);
            }
        }
        break;
    case AXIS_BENCH_WAIT_EXPIRING_START:
        if (AwaitApplied(&s, now)) Enter(AXIS_BENCH_WAIT_EXPIRY, now);
        break;
    case AXIS_BENCH_WAIT_EXPIRY:
        if (!s.active_valid)
        {
            if (s.permit || s.result != AXIS_CMD_EXPIRED ||
                s.result_seq != expected_seq ||
                s.run_epoch != expected_frame.run_epoch + 1U)
                Fail(AXIS_BENCH_ERR_EXPIRY, now);
            else
            {
                idle_epoch = s.run_epoch;
                axis_bench_pass_mask |= 0x10U;
                Enter(AXIS_BENCH_WAIT_IDLE, now);
            }
        }
        else if (!SameFrame(&s.active, &expected_frame))
            Fail(AXIS_BENCH_ERR_FRAME, now);
        else if ((uint32_t)(now - stage_started_ms) >= ACK_TIMEOUT_MS)
            Fail(AXIS_BENCH_ERR_ACK_TIMEOUT, now);
        break;
    case AXIS_BENCH_WAIT_IDLE:
    case AXIS_BENCH_PASS:
        if (s.active_valid || s.permit || s.run_epoch != idle_epoch ||
            s.applied_seq != expected_seq)
            Fail(AXIS_BENCH_ERR_RESTART, now);
        else if (stage == AXIS_BENCH_WAIT_IDLE &&
                 (uint32_t)(now - stage_started_ms) >= IDLE_OBSERVE_MS)
        {
            axis_bench_pass_mask |= 0x20U;
            Enter(AXIS_BENCH_PASS, now);
        }
        break;
    default:
        Fail(AXIS_BENCH_ERR_SNAPSHOT, now);
        break;
    }
}

#else
/* Production contains no automatic producer and no synthetic bench limits. */
void AxisCommandBench_Init(uint32_t now_ms) { (void)now_ms; }
void AxisCommandBench_Step(uint32_t now_ms, uint32_t ready,
                           uint32_t output_safe, uint32_t fault_mask,
                           uint32_t overruns)
{
    (void)now_ms; (void)ready; (void)output_safe; (void)fault_mask; (void)overruns;
}
#endif
