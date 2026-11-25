#include "axis_command_core.h"
#include <float.h>
#include <stddef.h>
#include <string.h>

typedef char AxisCommandFrame_must_be_32_bytes[(sizeof(AxisCommandFrame) == 32U) ? 1 : -1];
typedef char AxisCommandReceipt_must_be_12_bytes[(sizeof(AxisCommandReceipt) == 12U) ? 1 : -1];
typedef char AxisCommandReceipt_field_offsets[(
    offsetof(AxisCommandReceipt, command_seq) == 0U &&
    offsetof(AxisCommandReceipt, result) == 4U &&
    offsetof(AxisCommandReceipt, control_seq) == 8U) ? 1 : -1];
typedef char AxisCommandFloat_must_be_32_bits[(sizeof(float) == 4U) ? 1 : -1];
typedef struct { char padding; AxisCommandFrame frame; } FrameAlignmentProbe;
typedef char AxisCommandFrame_alignment[(offsetof(FrameAlignmentProbe, frame) % 4U == 0U) ? 1 : -1];
typedef char AxisCommandFrame_field_offsets[(
    offsetof(AxisCommandFrame, command_seq) == 0U &&
    offsetof(AxisCommandFrame, run_epoch) == 4U &&
    offsetof(AxisCommandFrame, opcode) == 8U &&
    offsetof(AxisCommandFrame, control_mode) == 12U &&
    offsetof(AxisCommandFrame, speed_target_rpm) == 16U &&
    offsetof(AxisCommandFrame, iq_limit_a) == 20U &&
    offsetof(AxisCommandFrame, received_tick_ms) == 24U &&
    offsetof(AxisCommandFrame, expires_tick_ms) == 28U) ? 1 : -1];

#define HALF_RANGE 0x80000000UL
#define COUNTER_MAX 0xffffffffUL

static uint32_t Finite(float value)
{
    return (value == value) && (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static uint32_t LimitsValid(const AxisCommandLimits *limits)
{
    return (limits != 0) && (limits->confirmed == 1U) &&
        Finite(limits->max_abs_speed_rpm) &&
        (limits->max_abs_speed_rpm > 0.0f) &&
        Finite(limits->max_iq_a) && (limits->max_iq_a > 0.0f) &&
        (limits->max_lifetime_ms != 0U) &&
        (limits->max_lifetime_ms < HALF_RANGE);
}

/* Reject future reception, expired ages and ambiguous half-range intervals.
 * The same HAL tick origin must be used for all three timestamps. */
static uint32_t TimeValid(const AxisCommandFrame *frame, uint32_t now,
                          uint32_t max_lifetime)
{
    uint32_t lifetime = frame->expires_tick_ms - frame->received_tick_ms;
    uint32_t age = now - frame->received_tick_ms;
    return (lifetime != 0U) && (lifetime < HALF_RANGE) &&
        (lifetime <= max_lifetime) && (age < lifetime);
}

static uint32_t Report(AxisCommandCore *c, uint32_t seq, uint32_t result)
{
    c->result_seq = seq;
    c->result = result;
    return result;
}

/* TIM1 only, before removing a pending frame. The single occupied mailbox
 * belongs to last_accepted_seq even when its frame sequence is invalid. Never
 * attribute a receipt to that corrupted field or overwrite a newer receipt.
 * Core metadata integrity is required; this is not memory-corruption recovery. */
static void RetainPendingReceipt(AxisCommandCore *c, uint32_t result)
{
    if ((c->last_accepted_seq != 0U) &&
        (c->last_accepted_seq > c->receipt.command_seq))
    {
        c->receipt.command_seq = c->last_accepted_seq;
        c->receipt.result = result;
        c->receipt.control_seq = c->control_seq;
    }
}

void AxisCommand_Init(AxisCommandCore *c, const AxisCommandLimits *limits)
{
    memset(c, 0, sizeof(*c));
    if (limits != 0) c->limits = *limits;
    c->config_valid = LimitsValid(limits);
    c->run_epoch = 1U;
    c->result = c->config_valid ? AXIS_CMD_OK : AXIS_CMD_CONFIG_UNSET;
}

uint32_t AxisCommand_ValidateTargets(const AxisCommandLimits *limits,
                                    float speed_target_rpm, float iq_limit_a)
{
    if (!LimitsValid(limits)) return AXIS_CMD_CONFIG_UNSET;
    if (!Finite(speed_target_rpm) || !Finite(iq_limit_a) ||
        (iq_limit_a < 0.0f) || (iq_limit_a > limits->max_iq_a) ||
        (speed_target_rpm > limits->max_abs_speed_rpm) ||
        (speed_target_rpm < -limits->max_abs_speed_rpm))
        return AXIS_CMD_INVALID;
    return AXIS_CMD_OK;
}

uint32_t AxisCommand_Validate(const AxisCommandLimits *limits,
                             const AxisCommandFrame *frame, uint32_t now)
{
    if (!LimitsValid(limits)) return AXIS_CMD_CONFIG_UNSET;
    if (frame == 0) return AXIS_CMD_INVALID;
    if (frame->command_seq != 0U) return AXIS_CMD_BAD_SEQUENCE;
    if (frame->control_mode != AXIS_CMD_SPEED_MODE)
        return AXIS_CMD_UNSUPPORTED_MODE;
    if (((frame->opcode != AXIS_CMD_START) && (frame->opcode != AXIS_CMD_UPDATE)) ||
        (frame->run_epoch == 0U) ||
        (AxisCommand_ValidateTargets(limits, frame->speed_target_rpm,
                                    frame->iq_limit_a) != AXIS_CMD_OK))
        return AXIS_CMD_INVALID;
    if (!TimeValid(frame, now, limits->max_lifetime_ms)) return AXIS_CMD_EXPIRED;
    return AXIS_CMD_OK;
}

uint32_t AxisCommand_PublishValidated(AxisCommandCore *c,
    const AxisCommandFrame *frame, uint32_t now, uint32_t fault, uint32_t *seq)
{
    uint32_t result = AXIS_CMD_OK;
    if (seq != 0) *seq = 0U;
    /* Float/range validation was performed on this same private frame before
     * entering the caller's PRIMASK section. Recheck changing integer state. */
    if ((fault != 0U) || (c->fault_latched != 0U)) result = AXIS_CMD_FAULT;
    else if (c->exhausted != 0U) result = AXIS_CMD_EXHAUSTED;
    else if (c->config_valid == 0U) result = AXIS_CMD_CONFIG_UNSET;
    else if (frame == 0) result = AXIS_CMD_INVALID;
    else if (frame->command_seq != 0U) result = AXIS_CMD_BAD_SEQUENCE;
    else if (frame->run_epoch != c->run_epoch) result = AXIS_CMD_STALE_EPOCH;
    else if (c->stop_pending != 0U) result = AXIS_CMD_STOP_PENDING;
    else if (!TimeValid(frame, now, c->limits.max_lifetime_ms)) result = AXIS_CMD_EXPIRED;
    else if (c->pending_ready != 0U) result = AXIS_CMD_BUSY;
    else if (c->last_accepted_seq == COUNTER_MAX)
    {
        c->exhausted = 1U;
        result = AXIS_CMD_EXHAUSTED;
    }
    if (result != AXIS_CMD_OK) return Report(c, 0U, result);
    c->last_accepted_seq++;
    c->pending = *frame;
    c->pending.command_seq = c->last_accepted_seq;
    c->pending_ready = 1U;
    if (seq != 0) *seq = c->last_accepted_seq;
    return Report(c, c->last_accepted_seq, AXIS_CMD_OK);
}

uint32_t AxisCommand_RequestStop(AxisCommandCore *c, uint32_t *ticket)
{
    if (c->stop_pending == 0U)
    {
        if (c->stop_ticket < COUNTER_MAX) c->stop_ticket++;
        else c->exhausted = 1U; /* Saturate, but still deliver STOP. */
        c->stop_pending = 1U;
    }
    if (ticket != 0) *ticket = c->stop_ticket;
    return AXIS_CMD_OK;
}

void AxisCommand_Revoke(AxisCommandCore *c, uint32_t reason)
{
    uint32_t advance = (c->active_valid != 0U) || (c->pending_ready != 0U) ||
        (reason == AXIS_CMD_STOPPED) ||
        ((reason == AXIS_CMD_FAULT) && (c->fault_latched == 0U));
    uint32_t seq = c->active_valid ? c->active.command_seq :
        (c->pending_ready ? c->pending.command_seq : 0U);
    if (c->pending_ready)
    {
        c->cancelled_seq = c->pending.command_seq;
        RetainPendingReceipt(c, reason);
    }
    c->active_valid = 0U;
    c->pending_ready = 0U;
    if (reason == AXIS_CMD_FAULT) c->fault_latched = 1U;
    if (advance != 0U)
    {
        if (c->run_epoch < COUNTER_MAX) c->run_epoch++;
        if (c->run_epoch == COUNTER_MAX) c->exhausted = 1U;
    }
    Report(c, seq, reason);
}

uint32_t AxisCommand_Permitted(const AxisCommandCore *c,
                              uint32_t now, uint32_t fault)
{
    return (fault == 0U) && (c->fault_latched == 0U) &&
        (c->exhausted == 0U) && (c->config_valid != 0U) &&
        (c->stop_pending == 0U) && (c->active_valid != 0U) &&
        (c->active.run_epoch == c->run_epoch) &&
        TimeValid(&c->active, now, c->limits.max_lifetime_ms);
}

uint32_t AxisCommand_NeedsDisarm(const AxisCommandCore *c,
                               uint32_t now, uint32_t fault)
{
    return !AxisCommand_Permitted(c, now, fault);
}

uint32_t AxisCommand_Guard(AxisCommandCore *c, uint32_t now, uint32_t fault)
{
    if ((fault != 0U) || (c->fault_latched != 0U))
    {
        if ((c->fault_latched == 0U) || c->active_valid || c->pending_ready)
            AxisCommand_Revoke(c, AXIS_CMD_FAULT);
        return 0U;
    }
    if (c->exhausted != 0U)
    {
        if (c->active_valid || c->pending_ready)
            AxisCommand_Revoke(c, AXIS_CMD_EXHAUSTED);
        return 0U;
    }
    if (c->config_valid == 0U)
    {
        if (c->active_valid || c->pending_ready)
            AxisCommand_Revoke(c, AXIS_CMD_CONFIG_UNSET);
        return 0U;
    }
    if (c->active_valid != 0U)
    {
        if (c->active.run_epoch != c->run_epoch)
            AxisCommand_Revoke(c, AXIS_CMD_STALE_EPOCH);
        else if (!TimeValid(&c->active, now, c->limits.max_lifetime_ms))
            AxisCommand_Revoke(c, AXIS_CMD_EXPIRED);
    }
    return AxisCommand_Permitted(c, now, fault);
}

void AxisCommand_Tick(AxisCommandCore *c, uint32_t now,
                      uint32_t fault, uint32_t ready)
{
    uint32_t result = AXIS_CMD_OK;
    uint32_t seq;
    /* Observation only: modulo counter, never an authorization/replay token. */
    c->control_seq++;
    /* Fault and STOP outrank expiry and ordinary commands. */
    if ((fault != 0U) || (c->fault_latched != 0U))
        (void)AxisCommand_Guard(c, now, fault);
    if (c->stop_pending != 0U)
    {
        /* Fault already invalidated this epoch. Still acknowledge the ticket. */
        if (c->fault_latched == 0U) AxisCommand_Revoke(c, AXIS_CMD_STOPPED);
        else Report(c, 0U, AXIS_CMD_STOPPED);
        c->stop_applied_ticket = c->stop_ticket;
        c->stop_pending = 0U;
        return;
    }
    (void)AxisCommand_Guard(c, now, fault);
    if (c->active_valid && !ready)
    {
        AxisCommand_Revoke(c, AXIS_CMD_NOT_READY);
        return;
    }
    if ((fault != 0U) || c->fault_latched || c->exhausted || !c->config_valid ||
        !c->pending_ready) return;
    seq = c->pending.command_seq;
    if ((seq == 0U) || (seq <= c->last_consumed_seq) ||
        (seq != c->last_accepted_seq)) result = AXIS_CMD_BAD_SEQUENCE;
    else
    {
        c->last_consumed_seq = seq;
        if (c->pending.run_epoch != c->run_epoch) result = AXIS_CMD_STALE_EPOCH;
        else if (!TimeValid(&c->pending, now, c->limits.max_lifetime_ms))
            result = AXIS_CMD_EXPIRED;
        else if (c->pending.control_mode != AXIS_CMD_SPEED_MODE)
            result = AXIS_CMD_UNSUPPORTED_MODE;
        else if (c->pending.opcode == AXIS_CMD_START)
        {
            if (c->active_valid) result = AXIS_CMD_ALREADY_RUNNING;
            else if (!ready) result = AXIS_CMD_NOT_READY;
        }
        else if (c->pending.opcode == AXIS_CMD_UPDATE)
        {
            if (!c->active_valid) result = AXIS_CMD_NOT_RUNNING;
        }
        else result = AXIS_CMD_INVALID;
    }
    if (result == AXIS_CMD_OK)
    {
        c->active = c->pending;
        c->active_valid = 1U;
        c->applied_seq = seq;
        c->applied_control_seq = c->control_seq;
        result = AXIS_CMD_APPLIED;
    }
    RetainPendingReceipt(c, result);
    c->pending_ready = 0U;
    Report(c, seq, result);
}

float AxisCommand_ClampIq(float value, float limit)
{
    if (!Finite(value) || !Finite(limit) || (limit < 0.0f)) return 0.0f;
    if (value > limit) return limit;
    if (value < -limit) return -limit;
    return value;
}
