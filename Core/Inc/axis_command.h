#ifndef AXIS_COMMAND_H
#define AXIS_COMMAND_H
#include "axis_command_core.h"

/* Init before scheduling. Submit/Stop only from appTask, with a stable local
 * request stamped at ingress using HAL_GetTick and the then-current epoch.
 * No protocol RX or automatic producer is installed by this module. */
typedef struct {
    AxisCommandFrame active;
    uint32_t active_valid;
    uint32_t run_epoch;
    uint32_t applied_seq;
    uint32_t applied_control_seq;
    uint32_t control_seq;
    uint32_t result_seq;
    uint32_t result;
    uint32_t cancelled_seq;
    uint32_t stop_applied_ticket;
    uint32_t permit;
} AxisCommandSnapshot;

/* appTask only: fixed-size coherent copy, not a reliable protocol ACK queue. */
uint32_t AxisCommandPort_GetSnapshot(AxisCommandSnapshot *snapshot);
/* appTask only: coherent 12-byte handling history, not current permission.
 * command_seq=0 means none. Read does not clear/acknowledge the retained record.
 * NULL/wrong context returns AXIS_CMD_INVALID and leaves the output untouched.
 * Future CAN must cache a result before submitting the next ordinary request. */
uint32_t AxisCommandPort_GetReceipt(AxisCommandReceipt *receipt);
void AxisCommandPort_Init(void);
/* appTask only; read-only config/range check using the same immutable limits.
 * Does NOT check current fault/ready/epoch/time, publish, or renew permission.
 * Wrong task/ISR/kernel context returns AXIS_CMD_INVALID_CONTEXT. */
uint32_t AxisCommandPort_CheckTargets(float speed_target_rpm, float iq_limit_a);
uint32_t AxisCommandPort_Submit(const AxisCommandFrame *request, uint32_t *seq);
/* appTask only; Bench-only hardware-timed publication. origin_tick is the
 * original first-frame SOF (0..65535), lifetime_ticks is 1..31250 at nominal
 * 32 us/tick. The two caller HAL timestamp fields are ignored: an auxiliary
 * HAL window keeps the unchanged pure core contract; it NEVER replaces or
 * extends the original hardware deadline. Ordinary profile is unsupported. */
uint32_t AxisCommandPort_SubmitTimed(const AxisCommandFrame *request,
    uint32_t origin_tick, uint32_t lifetime_ticks, uint32_t *seq);
/* appTask only, before every CanLink_Poll: latch expired timed sequences even
 * if TIM1 is not serviced. Does not modify core state or outputs. The <=200 ms
 * service-progress contract prevents raw16 wrap from making a lease young again.
 * TIM1 remains the only consumer that disarms/revokes; ordinary profile is no-op. */
void AxisCommandPort_ObserveTime(void);
uint32_t AxisCommandPort_Stop(uint32_t *ticket);
uint32_t AxisCommandPort_GetEpoch(void);

/* TIM1 IRQ0 only. Borrowed frame pointer is valid only within this ISR.
 * board_safety performs hardware disarm BEFORE revoking software state. */
const AxisCommandFrame *AxisCommandPort_BeginCycle(uint32_t now, uint32_t ready);
uint32_t AxisCommandPort_FinalPermit(uint32_t now);
void AxisCommandPort_RejectActive(uint32_t reason);

extern volatile uint32_t axis_command_configured;
extern volatile uint32_t axis_command_epoch;
extern volatile uint32_t axis_command_permit;
extern volatile uint32_t axis_command_applied_seq;
extern volatile uint32_t axis_command_result;
extern volatile uint32_t axis_command_result_seq;
extern volatile uint32_t axis_command_cancelled_seq;
extern volatile uint32_t axis_command_stop_applied;
extern volatile uint32_t axis_command_control_seq;
extern volatile uint32_t axis_command_critical_max_cycles;
#endif
