#ifndef AXIS_COMMAND_CORE_H
#define AXIS_COMMAND_CORE_H

#include <stdint.h>

enum {
    AXIS_CMD_OK = 0U, AXIS_CMD_CONFIG_UNSET, AXIS_CMD_INVALID,
    AXIS_CMD_UNSUPPORTED_MODE, AXIS_CMD_EXPIRED, AXIS_CMD_BUSY,
    AXIS_CMD_STALE_EPOCH, AXIS_CMD_BAD_SEQUENCE, AXIS_CMD_STOP_PENDING,
    AXIS_CMD_FAULT, AXIS_CMD_NOT_RUNNING, AXIS_CMD_ALREADY_RUNNING,
    AXIS_CMD_NOT_READY, AXIS_CMD_EXHAUSTED, AXIS_CMD_APPLIED,
    AXIS_CMD_STOPPED, AXIS_CMD_INVALID_CONTEXT
};
enum { AXIS_CMD_START = 1U, AXIS_CMD_UPDATE = 2U,
       AXIS_CMD_SPEED_MODE = 1U };

typedef struct {
    uint32_t command_seq;
    uint32_t run_epoch;
    uint32_t opcode;
    uint32_t control_mode;
    float speed_target_rpm;
    float iq_limit_a;
    uint32_t received_tick_ms;
    uint32_t expires_tick_ms;
} AxisCommandFrame;

typedef struct {
    uint32_t confirmed;
    float max_abs_speed_rpm;
    float max_iq_a;
    uint32_t max_lifetime_ms;
} AxisCommandLimits;

/* Most recently processed ordinary command, not current running permission.
 * seq=0 means none. APPLIED records core installation, not FOC/output success.
 * Only a newer processed pending command replaces this single slot. A future
 * protocol must save its previous receipt before submitting another command;
 * this is not an event queue or storage across reset. */
typedef struct {
    uint32_t command_seq;
    uint32_t result;
    uint32_t control_seq; /* Observation only; may wrap, never authorization. */
} AxisCommandReceipt;

typedef struct {
    AxisCommandLimits limits;
    uint32_t config_valid;
    AxisCommandFrame pending;
    AxisCommandFrame active;
    uint32_t pending_ready;
    uint32_t active_valid;
    uint32_t run_epoch;
    uint32_t last_accepted_seq;
    uint32_t last_consumed_seq;
    uint32_t control_seq;
    uint32_t applied_seq;
    uint32_t applied_control_seq;
    uint32_t result_seq;
    uint32_t result;
    /* Most recent accepted pending frame cancelled by Revoke (0 = none).
     * result_seq names the revoked active command when both existed. */
    uint32_t cancelled_seq;
    uint32_t stop_pending;
    uint32_t stop_ticket;
    uint32_t stop_applied_ticket;
    uint32_t fault_latched;
    uint32_t exhausted;
    AxisCommandReceipt receipt;
} AxisCommandCore;

/* No platform calls or internal locks. Init occurs before IRQ startup.
 * A single task validates a PRIVATE immutable frame outside PRIMASK, then
 * publishes exactly that frame under PRIMASK. RequestStop uses that same lock.
 * TIM1 alone calls Tick/Revoke and owns active/run_epoch. Readers take the
 * caller's snapshot lock; volatile alone is not a publication protocol.
 * Hardware must be disabled BEFORE Tick/Revoke when NeedsDisarm returns true.
 * Final output permission must also call Permitted with current HAL time/fault.
 */
void AxisCommand_Init(AxisCommandCore *c, const AxisCommandLimits *limits);
/* Pure range/config precheck only. No epoch, freshness, readiness or permission.
 * Shared by full frame validation and diagnostic CAN pair precheck. */
uint32_t AxisCommand_ValidateTargets(const AxisCommandLimits *limits,
                                    float speed_target_rpm, float iq_limit_a);
uint32_t AxisCommand_Validate(const AxisCommandLimits *limits,
                             const AxisCommandFrame *frame, uint32_t now);
uint32_t AxisCommand_PublishValidated(AxisCommandCore *c,
    const AxisCommandFrame *frame, uint32_t now, uint32_t fault, uint32_t *seq);
uint32_t AxisCommand_RequestStop(AxisCommandCore *c, uint32_t *ticket);
uint32_t AxisCommand_NeedsDisarm(const AxisCommandCore *c,
                               uint32_t now, uint32_t fault);
void AxisCommand_Tick(AxisCommandCore *c, uint32_t now,
                      uint32_t fault, uint32_t ready);
void AxisCommand_Revoke(AxisCommandCore *c, uint32_t reason);
/* Final TIM1 gate: no cycle increment, floating-point work or consumption. */
uint32_t AxisCommand_Guard(AxisCommandCore *c, uint32_t now, uint32_t fault);
uint32_t AxisCommand_Permitted(const AxisCommandCore *c,
                              uint32_t now, uint32_t fault);
float AxisCommand_ClampIq(float value, float limit);

#endif
