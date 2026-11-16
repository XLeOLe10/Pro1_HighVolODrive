#ifndef CAN_LINK_PROTOCOL_H
#define CAN_LINK_PROTOCOL_H

#include <stdint.h>

#define CAN_LINK_REQUEST_ID 0x321U
#define CAN_LINK_REPLY_ID   0x322U

/* Private read-only diagnostic protocol, not ODrive CANSimple. */
#define CAN_QUERY_INFO     0x01U
#define CAN_QUERY_STATUS   0x02U
#define CAN_QUERY_FAULT    0x03U
#define CAN_QUERY_BUILD    0x04U
#define CAN_QUERY_INFO_VALUE     0x0001001FUL
/* Manual interface release 008.01; NOT an automatically generated image hash. */
#define CAN_QUERY_BUILD_REVISION 0x00000801UL

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

/* Request: 51 01 opcode token 00 00 00 00, using the same standard ID/DLC.
 * Returns one of CAN_QUERY_* or 0. Rejects unknown fields without side effects. */
uint8_t CanLinkProtocol_QueryOpcode(const CanLinkFrame *request);
/* Reply: 52 01 opcode token value_u32_little_endian.
 * Invalid requests leave reply untouched. request/reply may alias. */
int CanLinkProtocol_MakeQueryReply(const CanLinkFrame *request,
                                  uint32_t value, CanLinkFrame *reply);

#endif
