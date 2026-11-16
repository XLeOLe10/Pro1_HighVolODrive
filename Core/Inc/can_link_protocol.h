#ifndef CAN_LINK_PROTOCOL_H
#define CAN_LINK_PROTOCOL_H

#include <stdint.h>

#define CAN_LINK_REQUEST_ID 0x321U
#define CAN_LINK_REPLY_ID   0x322U

typedef struct {
    uint32_t id;
    uint8_t extended;
    uint8_t remote;
    uint8_t fd;
    uint8_t brs;
    uint8_t length;
    uint8_t data[8];
} CanLinkFrame;

/* Pure link probe, with no motor-control side effects. Metadata flags are zero
 * for standard, data, classic, no-BRS frames; any nonzero flag is rejected.
 * Returns 1 for a valid PING and a complete PONG, otherwise 0 without changing
 * reply. request and reply may alias. Buffers are owned by the caller. */
int CanLinkProtocol_MakeReply(const CanLinkFrame *request, CanLinkFrame *reply);

#endif
