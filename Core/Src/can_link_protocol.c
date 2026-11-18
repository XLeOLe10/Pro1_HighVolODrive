#include "can_link_protocol.h"
#include <stddef.h>

int CanLinkProtocol_MakeReply(const CanLinkFrame *request, CanLinkFrame *reply)
{
    CanLinkFrame result = {0U, 0U, 0U, 0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    unsigned int i;

    if ((request == NULL) || (reply == NULL)) return 0;
    if ((request->id != CAN_LINK_REQUEST_ID) ||
        (request->extended != 0U) || (request->remote != 0U) ||
        (request->fd != 0U) || (request->brs != 0U) ||
        (request->length != 8U)) return 0;
    if ((request->data[0] != 0x50U) || (request->data[1] != 0x49U) ||
        (request->data[2] != 0x4EU) || (request->data[3] != 0x47U)) return 0;

    result.id = CAN_LINK_REPLY_ID;
    result.length = 8U;
    result.data[0] = 0x50U;
    result.data[1] = 0x4FU;
    result.data[2] = 0x4EU;
    result.data[3] = 0x47U;
    for (i = 4U; i < 8U; ++i) result.data[i] = request->data[i];
    *reply = result;
    return 1;
}

uint8_t CanLinkProtocol_QueryOpcode(const CanLinkFrame *request)
{
    unsigned int i;
    if (request == NULL) return 0U;
    if ((request->id != CAN_LINK_REQUEST_ID) ||
        (request->extended != 0U) || (request->remote != 0U) ||
        (request->fd != 0U) || (request->brs != 0U) ||
        (request->length != 8U)) return 0U;
    if ((request->data[0] != 0x51U) || (request->data[1] != 0x01U) ||
        (request->data[2] < CAN_QUERY_INFO) ||
        (request->data[2] > CAN_QUERY_RX_AGE_US)) return 0U;
    for (i = 4U; i < 8U; ++i)
        if (request->data[i] != 0U) return 0U;
    return request->data[2];
}

int CanLinkProtocol_MakeQueryReply(const CanLinkFrame *request,
                                  uint32_t value, CanLinkFrame *reply)
{
    CanLinkFrame result = {0U, 0U, 0U, 0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    uint8_t opcode;
    unsigned int i;
    if (reply == NULL) return 0;
    opcode = CanLinkProtocol_QueryOpcode(request);
    if (opcode == 0U) return 0;
    result.id = CAN_LINK_REPLY_ID;
    result.length = 8U;
    result.data[0] = 0x52U;
    result.data[1] = 0x01U;
    result.data[2] = opcode;
    result.data[3] = request->data[3];
    for (i = 0U; i < 4U; ++i)
        result.data[4U + i] = (uint8_t)(value >> (i * 8U));
    *reply = result;
    return 1;
}

uint32_t CanLinkProtocol_StopRequestId(const CanLinkFrame *request)
{
    if (request == NULL) return 0U;
    if ((request->id != CAN_LINK_REQUEST_ID) ||
        (request->extended != 0U) || (request->remote != 0U) ||
        (request->fd != 0U) || (request->brs != 0U) ||
        (request->length != 8U)) return 0U;
    if ((request->data[0] != 0x43U) || (request->data[1] != 0x01U) ||
        (request->data[2] != CAN_STOP_OPCODE) || (request->data[3] != 0U)) return 0U;
    return (uint32_t)request->data[4] | ((uint32_t)request->data[5] << 8U) |
           ((uint32_t)request->data[6] << 16U) | ((uint32_t)request->data[7] << 24U);
}

int CanLinkProtocol_MakeStopReply(const CanLinkFrame *request,
                                 uint8_t status, CanLinkFrame *reply)
{
    CanLinkFrame result = {0U, 0U, 0U, 0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    uint32_t request_id;
    unsigned int i;
    if ((reply == NULL) || (status < CAN_STOP_ACCEPTED) ||
        (status > CAN_STOP_REJECTED)) return 0;
    request_id = CanLinkProtocol_StopRequestId(request);
    if (request_id == 0U) return 0;
    result.id = CAN_LINK_REPLY_ID;
    result.length = 8U;
    result.data[0] = 0x41U;
    result.data[1] = 0x01U;
    result.data[2] = CAN_STOP_OPCODE;
    result.data[3] = status;
    for (i = 0U; i < 4U; ++i)
        result.data[4U + i] = (uint8_t)(request_id >> (i * 8U));
    *reply = result;
    return 1;
}

int CanLinkProtocol_ReadTargetCheck(const CanLinkFrame *request, CanLinkTargetPair *pair)
{
    CanLinkTargetPair decoded;
    uint32_t speed;
    if ((request == NULL) || (pair == NULL)) return 0;
    if ((request->id != CAN_LINK_REQUEST_ID) ||
        (request->extended != 0U) || (request->remote != 0U) ||
        (request->fd != 0U) || (request->brs != 0U) ||
        (request->length != 8U)) return 0;
    if ((request->data[0] != 0x43U) || (request->data[1] != 0x01U) ||
        (request->data[2] != CAN_TARGET_CHECK_OPCODE)) return 0;
    speed = (uint32_t)request->data[4] | ((uint32_t)request->data[5] << 8U);
    /* Explicit sign extension avoids implementation-defined uint16 -> int16. */
    decoded.speed_rpm = (speed >= 0x8000U) ? ((int32_t)speed - 65536) : (int32_t)speed;
    decoded.iq_limit_ma = (uint32_t)request->data[6] | ((uint32_t)request->data[7] << 8U);
    *pair = decoded;
    return 1;
}

int CanLinkProtocol_MakeTargetCheckReply(const CanLinkFrame *request,
                                        uint8_t result_code, CanLinkFrame *reply)
{
    CanLinkFrame result = {0U, 0U, 0U, 0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    CanLinkTargetPair pair;
    unsigned int i;
    if ((reply == NULL) || (result_code < CAN_TARGET_CHECK_OK) ||
        (result_code > CAN_TARGET_CHECK_REJECTED) ||
        (CanLinkProtocol_ReadTargetCheck(request, &pair) == 0)) return 0;
    result.id = CAN_LINK_REPLY_ID;
    result.length = 8U;
    result.data[0] = 0x56U;
    result.data[1] = 0x01U;
    result.data[2] = result_code;
    for (i = 3U; i < 8U; ++i) result.data[i] = request->data[i];
    *reply = result;
    return 1;
}
