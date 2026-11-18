#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <stdint.h>

/* appTask is the only writer/user. Debug scalars are not a coherent snapshot. */
extern volatile uint32_t can_link_rx_count; /* accepted PINGs, not all bus frames */
extern volatile uint32_t can_query_rx_count; /* accepted read queries, not TX success */
extern volatile uint32_t can_link_error_count; /* observed error events, may coalesce */
typedef struct {
    uint32_t state; /* 0=not started, 1=ready, 2=failed, 3=bus-off latched */
    uint32_t error_flags; /* latched CAN_LINK_ERROR_* */
    uint32_t rejected_count;
    uint32_t tx_enqueued_count; /* NOT proof of delivery */
    uint32_t tx_busy_count;
    uint32_t rx_lost_events;
    uint32_t protocol_error_events;
    uint32_t last_ir;
    uint32_t last_lec;
    uint32_t tx_error_count;
    uint32_t rx_error_count;
} CanLinkDiagnostics;
extern volatile CanLinkDiagnostics can_link_diag;

#define CAN_TIME_NOT_READY      0U
#define CAN_TIME_TRACKING       1U
#define CAN_TIME_LOST           2U
#define CAN_TIME_LOSS_TIMEOUT   0x01U
#define CAN_TIME_LOSS_CONFIG    0x02U
#define CAN_TIME_LOSS_RX_LOST   0x04U
#define CAN_TIME_LOSS_HAL       0x08U
#define CAN_TIME_LOSS_LINK      0x10U
#define CAN_TIME_TICK_US        32U
#define CAN_TIME_SERVICE_TICKS  6250U

typedef struct {
    uint32_t state;
    uint32_t reason_flags; /* Sticky CAN_TIME_LOSS_*; no runtime recovery. */
    /* Last successful HAL dequeue's trusted age observation, not a running
     * clock/current frame age. Empty polls retain it; invalid/unknown or direct
     * non-8-DLC discard gives UINT32_MAX. This is never control permission. */
    uint32_t last_rx_age_us;
} CanLinkTimeDiagnostics;
/* Writable debugger mirrors only. Private appTask state owns decisions and
 * query values; editing these fields cannot restore trusted time. */
extern volatile CanLinkTimeDiagnostics can_link_time_diag;

#define CAN_LINK_ERROR_INIT       0x01U
#define CAN_LINK_ERROR_RX_LOST    0x02U
#define CAN_LINK_ERROR_PROTOCOL   0x04U
#define CAN_LINK_ERROR_BUS_OFF    0x08U
#define CAN_LINK_ERROR_RAM        0x10U
#define CAN_LINK_ERROR_HAL        0x20U
#define CAN_LINK_ERROR_TX_BUSY    0x40U
#define CAN_LINK_ERROR_CONFIG     0x80U

/* Diagnostic mirrors copied without preemption, NOT one physical/control
 * sample and NOT permission to run. permit is the last published observation.
 * The provider is appTask-only, fixed-size/nonblocking, and may not change
 * commands, outputs or faults. A null provider fails one-shot initialization. */
typedef struct {
    uint32_t status_word;
    uint32_t fault_mask;
    uint32_t configured;
    uint32_t permit;
    uint32_t output_enabled;
    /* Monotonic completion marker, published only AFTER TIM1 handles STOP.
     * May lag the core; unlike other mirrors, matching a retained ticket is
     * proof that this boot's STOP was handled, not that the motor is stationary. */
    uint32_t stop_applied_ticket;
} CanLinkReadStatus;
typedef void (*CanLinkStatusReader)(CanLinkReadStatus *snapshot);
/* appTask only, bounded/nonblocking. Returns 1 with a nonzero ticket only
 * after the independent STOP channel accepts; otherwise 0. No start/clear. */
typedef uint32_t (*CanLinkStopRequester)(uint32_t *ticket);
/* Read-only, appTask, bounded; returns one CAN_TARGET_CHECK_* wire status.
 * Must NOT publish/control/renew a command, or retain an approved target. */
typedef uint8_t (*CanLinkTargetChecker)(int32_t speed_rpm, uint32_t iq_limit_ma);
/* Read-only immutable boot identity, generated before tasks start. Returns
 * 0=not ready, 1=valid, 2=failed; fills four words without waiting for RNG.
 * Queries never refresh identity, authorize control or change a command. */
typedef uint32_t (*CanLinkBootReader)(uint32_t words[4]);

/* Bench-only, one startup call in appTask. No retry/recovery during this test. */
void CanLink_Init(CanLinkStatusReader read_status, CanLinkStopRequester request_stop,
                  CanLinkTargetChecker check_targets, CanLinkBootReader read_boot_identity);
/* After resolver acquisition: <=1 receive, <=1 enqueue, no waits/heap.
 * STATUS/FAULT/STOP-result uses bounded PRIMASK copies, encoding/HAL outside.
 * Only one control producer per boot. STOP IDs never wrap; the latest accepted
 * ID/ticket is retained even if TX fails. Reboot clears this history.
 * Healthy service reloads a hardware progress timer; this does not renew a
 * motor-command lease. Timestamp/timeout observations never authorize control. */
void CanLink_Poll(void);
#endif
