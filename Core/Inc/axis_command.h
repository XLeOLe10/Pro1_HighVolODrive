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
void AxisCommandPort_Init(void);
uint32_t AxisCommandPort_Submit(const AxisCommandFrame *request, uint32_t *seq);
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
