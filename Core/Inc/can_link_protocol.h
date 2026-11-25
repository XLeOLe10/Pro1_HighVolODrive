#ifndef CAN_LINK_PROTOCOL_H
#define CAN_LINK_PROTOCOL_H

#include <stdint.h>

#define CAN_LINK_REQUEST_ID 0x321U
#define CAN_LINK_REPLY_ID   0x322U

/* Private diagnostic / STOP protocol, not ODrive CANSimple. */
#define CAN_QUERY_INFO     0x01U
#define CAN_QUERY_STATUS   0x02U
#define CAN_QUERY_FAULT    0x03U
#define CAN_QUERY_BUILD    0x04U
#define CAN_QUERY_BOOT_STATE 0x05U
#define CAN_QUERY_BOOT_WORD0 0x06U
#define CAN_QUERY_BOOT_WORD1 0x07U
#define CAN_QUERY_BOOT_WORD2 0x08U
#define CAN_QUERY_BOOT_WORD3 0x09U
#define CAN_QUERY_TIME_STATE 0x0AU
#define CAN_QUERY_RX_AGE_US  0x0BU
#define CAN_QUERY_EPOCH      0x0CU
#define CAN_BOOT_STATE_NOT_READY 0U
#define CAN_BOOT_STATE_VALID     1U
#define CAN_BOOT_STATE_FAILED    2U
#define CAN_QUERY_INFO_VALUE     0x000103FFUL
/* Manual interface release 008.17; NOT an automatically generated image hash. */
#define CAN_QUERY_BUILD_REVISION 0x00000817UL

#define CAN_STOP_OPCODE    0x10U
#define CAN_STOP_ACCEPTED  1U
#define CAN_STOP_PROCESSED 2U
#define CAN_STOP_STALE     3U
#define CAN_STOP_BUSY      4U
#define CAN_STOP_REJECTED  5U

#define CAN_TARGET_CHECK_OPCODE   0x20U
#define CAN_TARGET_CHECK_OK       1U
#define CAN_TARGET_CHECK_RANGE    2U
#define CAN_TARGET_CHECK_CONFIG   3U
#define CAN_TARGET_CHECK_REJECTED 4U

#define CAN_TXN_PAGE_BEGIN   0x30U
#define CAN_TXN_PAGE_BOOT0   0x31U
#define CAN_TXN_PAGE_BOOT1   0x32U
#define CAN_TXN_PAGE_BOOT2   0x33U
#define CAN_TXN_PAGE_BOOT3   0x34U
#define CAN_TXN_PAGE_EPOCH   0x35U
#define CAN_TXN_PAGE_TARGETS 0x36U
#define CAN_TXN_PAGE_META    0x37U
#define CAN_TXN_PAGE_COMMIT  0x38U
#define CAN_TXN_PAGE_RESULT  0x39U
#define CAN_TXN_STAGING      1U
#define CAN_TXN_ACCEPTED     2U
#define CAN_TXN_CORE_RESULT  3U
#define CAN_TXN_REJECTED     4U
#define CAN_TXN_BUSY         5U
#define CAN_TXN_STALE        6U
#define CAN_TXN_UNKNOWN      7U
#define CAN_TXN_ERR_ORDER    1U
#define CAN_TXN_ERR_CONFLICT 2U
#define CAN_TXN_ERR_BOOT     3U
#define CAN_TXN_ERR_EPOCH    4U
#define CAN_TXN_ERR_CRC      5U
#define CAN_TXN_ERR_TTL      6U
#define CAN_TXN_ERR_TIME     7U
#define CAN_TXN_ERR_STOP     8U
#define CAN_TXN_ERR_FAULT    9U
#define CAN_TXN_ERR_INTERNAL 10U
#define CAN_TXN_ERR_OPCODE   11U
#define CAN_TXN_CORE_RESULT_MAX 16U
#define CAN_TXN_MAX_TTL_MS   1000U
#define CAN_TXN_MAX_TICKS    31250U

typedef struct {
    uint8_t page;
    uint8_t token;
    uint32_t value;
} CanLinkTransactionFrame;

typedef struct {
    int32_t speed_rpm;
    uint32_t iq_limit_ma;
} CanLinkTargetPair;

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

/* STOP: 43 01 10 00 request_id_u32_LE, nonzero monotonically increasing ID
 * within this boot. Same ID means reread retained result, NOT another stop.
 * Returns 0 on any invalid metadata/field. Does not execute commands. */
uint32_t CanLinkProtocol_StopRequestId(const CanLinkFrame *request);
/* Reply: 41 01 10 status request_id_u32_LE. Status must be CAN_STOP_*.
 * Invalid input leaves reply unchanged; request and reply may alias. */
int CanLinkProtocol_MakeStopReply(const CanLinkFrame *request,
                                 uint8_t status, CanLinkFrame *reply);

/* Precheck only: 43 01 20 token speed_i16_rpm_LE iq_u16_mA_LE.
 * Full payload decoded before publishing the caller-owned pair. No clamping,
 * no command/lease, token is only reply correlation. Invalid leaves pair intact. */
int CanLinkProtocol_ReadTargetCheck(const CanLinkFrame *request, CanLinkTargetPair *pair);
/* 56 01 result token + original four target bytes; NOT a control ACK.
 * Result is CAN_TARGET_CHECK_*. Invalid leaves reply intact; alias allowed. */
int CanLinkProtocol_MakeTargetCheckReply(const CanLinkFrame *request,
                                        uint8_t result, CanLinkFrame *reply);

/* V2: 43 02 page token value32LE. Pages30..37 form eight ordered words;
 * 38 commits CRC32 over those 32 wire-order bytes, 39 reads an explicit ID.
 * Token is only an assembly label, never identity/authorization. */
int CanLinkProtocol_ReadTransaction(const CanLinkFrame *request,
                                    CanLinkTransactionFrame *decoded);
/* IEEE reflected CRC32, init/final XOR FFFFFFFF, polynomial EDB88320.
 * Words are serialized little-endian; CRC is error detection, not security. */
uint32_t CanLinkProtocol_TransactionCrc32(const uint32_t words[8]);
/* 41 02 phase detail request_id32LE. Phase1 detail=next page; phase3 detail
 * is a core result (0E records installation), phase4 detail=CAN_TXN_ERR_*.
 * Invalid arguments leave reply untouched. ID0 is allowed only for UNKNOWN. */
int CanLinkProtocol_MakeTransactionReply(uint32_t request_id, uint8_t phase,
                                        uint8_t detail, CanLinkFrame *reply);

#endif
