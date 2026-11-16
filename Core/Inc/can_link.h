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
} CanLinkReadStatus;
typedef void (*CanLinkStatusReader)(CanLinkReadStatus *snapshot);

/* Bench-only, one startup call in appTask. No retry/recovery during this test. */
void CanLink_Init(CanLinkStatusReader read_status);
/* After resolver acquisition: <=1 receive, <=1 enqueue, no waits/heap.
 * STATUS/FAULT uses the provider's bounded PRIMASK copy; encoding/HAL outside. */
void CanLink_Poll(void);
#endif
