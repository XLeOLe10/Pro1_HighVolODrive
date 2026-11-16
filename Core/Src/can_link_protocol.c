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
