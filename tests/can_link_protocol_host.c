#include "can_link_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static CanLinkFrame Ping(void)
{
    CanLinkFrame frame = {0U, 0U, 0U, 0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    frame.id = CAN_LINK_REQUEST_ID;
    frame.length = 8U;
    frame.data[0] = 0x50U;
    frame.data[1] = 0x49U;
    frame.data[2] = 0x4EU;
    frame.data[3] = 0x47U;
    frame.data[4] = 0x00U;
    frame.data[5] = 0x80U;
    frame.data[6] = 0xFFU;
    frame.data[7] = 0x01U;
    return frame;
}

static void ExpectRejected(const CanLinkFrame *request)
{
    CanLinkFrame reply;
    unsigned char original[sizeof(reply)];
    memset(&reply, 0xA5, sizeof(reply));
    memcpy(original, &reply, sizeof(reply));
    CHECK(CanLinkProtocol_MakeReply(request, &reply) == 0);
    CHECK(memcmp(original, &reply, sizeof(reply)) == 0);
}

static void CheckReply(const CanLinkFrame *reply, const uint8_t sequence[4])
{
    static const uint8_t pong[4] = {0x50U, 0x4FU, 0x4EU, 0x47U};
    CHECK(reply->id == CAN_LINK_REPLY_ID);
    CHECK(reply->extended == 0U);
    CHECK(reply->remote == 0U);
    CHECK(reply->fd == 0U);
    CHECK(reply->brs == 0U);
    CHECK(reply->length == 8U);
    CHECK(memcmp(reply->data, pong, 4U) == 0);
    CHECK(memcmp(reply->data + 4U, sequence, 4U) == 0);
}

static void ValidFrames(void)
{
    CanLinkFrame request = Ping();
    CanLinkFrame reply;
    unsigned char original[sizeof(request)];
    uint8_t sequence[4];
    unsigned int value;
    unsigned int position;

    /* Each sequence byte is opaque, including NUL and high-bit values. */
    for (position = 4U; position < 8U; ++position) {
        for (value = 0U; value < 256U; ++value) {
            request = Ping();
            request.data[position] = (uint8_t)value;
            memcpy(original, &request, sizeof(request));
            memset(&reply, 0xA5, sizeof(reply));
            CHECK(CanLinkProtocol_MakeReply(&request, &reply) == 1);
            CheckReply(&reply, request.data + 4U);
            CHECK(memcmp(original, &request, sizeof(request)) == 0);
        }
    }

    request = Ping();
    memcpy(sequence, request.data + 4U, sizeof(sequence));
    CHECK(CanLinkProtocol_MakeReply(&request, &request) == 1);
    CheckReply(&request, sequence);
}

static void RejectedFrames(void)
{
    static const uint32_t wrong_ids[] = {
        0U, 0x320U, 0x322U, 0x7FFU, 0x800U, 0x1FFFFFFFU, 0xFFFFFFFFU
    };
    static const uint8_t wrong_flags[] = {1U, 2U, 255U};
    CanLinkFrame request;
    unsigned char original[sizeof(request)];
    unsigned int value;
    unsigned int position;

    for (value = 0U; value < sizeof(wrong_ids) / sizeof(wrong_ids[0]); ++value) {
        request = Ping();
        request.id = wrong_ids[value];
        ExpectRejected(&request);
    }
    for (value = 0U; value < sizeof(wrong_flags) / sizeof(wrong_flags[0]); ++value) {
        request = Ping(); request.extended = wrong_flags[value]; ExpectRejected(&request);
        request = Ping(); request.remote = wrong_flags[value]; ExpectRejected(&request);
        request = Ping(); request.fd = wrong_flags[value]; ExpectRejected(&request);
        request = Ping(); request.brs = wrong_flags[value]; ExpectRejected(&request);
    }
    for (value = 0U; value < 256U; ++value) {
        if (value == 8U) continue;
        request = Ping();
        request.length = (uint8_t)value;
        ExpectRejected(&request);
    }
    /* Every single-bit corruption of the marker must be rejected. */
    for (position = 0U; position < 4U; ++position) {
        for (value = 0U; value < 8U; ++value) {
            request = Ping();
            request.data[position] ^= (uint8_t)(1U << value);
            ExpectRejected(&request);
        }
    }
    /* PONG cannot elicit another reply. */
    request = Ping();
    request.data[1] = 0x4FU;
    ExpectRejected(&request);

    ExpectRejected(NULL);
    request = Ping();
    CHECK(CanLinkProtocol_MakeReply(&request, NULL) == 0);
    CHECK(CanLinkProtocol_MakeReply(NULL, NULL) == 0);

    request = Ping();
    request.length = 7U;
    memcpy(original, &request, sizeof(request));
    CHECK(CanLinkProtocol_MakeReply(&request, &request) == 0);
    CHECK(memcmp(original, &request, sizeof(request)) == 0);
}


/* Wire contract tests use literal packets/bytes, independent of the encoder. */
static CanLinkFrame Query(uint8_t opcode, uint8_t token)
{
    CanLinkFrame request = Ping();
    static const uint8_t prefix[8] = {0x51U, 0x01U, 0x01U, 0U, 0U, 0U, 0U, 0U};
    memcpy(request.data, prefix, sizeof(prefix));
    request.data[2] = opcode;
    request.data[3] = token;
    return request;
}

static void CheckQueryReply(const CanLinkFrame *reply, const uint8_t data[8])
{
    CHECK(reply->id == 0x322U);
    CHECK(reply->extended == 0U && reply->remote == 0U);
    CHECK(reply->fd == 0U && reply->brs == 0U);
    CHECK(reply->length == 8U);
    CHECK(memcmp(reply->data, data, 8U) == 0);
}

static void RejectQuery(const CanLinkFrame *request)
{
    CanLinkFrame reply;
    unsigned char before[sizeof(reply)];
    memset(&reply, 0x6D, sizeof(reply));
    memcpy(before, &reply, sizeof(reply));
    CHECK(CanLinkProtocol_QueryOpcode(request) == 0U);
    CHECK(CanLinkProtocol_MakeQueryReply(request, 0x01234567U, &reply) == 0);
    CHECK(memcmp(&reply, before, sizeof(reply)) == 0);
}

static void ValidQueries(void)
{
    static const uint32_t values[] = {
        0U, 0xFFFFFFFFU, 0x01234567U, 0x80000000U, 0x0001001FU
    };
    static const uint8_t value_bytes[][4] = {
        {0U,0U,0U,0U}, {0xFFU,0xFFU,0xFFU,0xFFU},
        {0x67U,0x45U,0x23U,0x01U}, {0U,0U,0U,0x80U},
        {0x1FU,0U,0x01U,0U}
    };
    CanLinkFrame request, reply;
    unsigned char before[sizeof(request)];
    unsigned opcode, token, v;
    uint8_t expected[8] = {0x52U,0x01U,0U,0U,0U,0U,0U,0U};
    CHECK(CAN_QUERY_INFO == 1U && CAN_QUERY_STATUS == 2U);
    CHECK(CAN_QUERY_FAULT == 3U && CAN_QUERY_BUILD == 4U);
    CHECK(CAN_QUERY_INFO_VALUE == 0x0001001FU);
    CHECK(CAN_QUERY_BUILD_REVISION == 0x00000801U);
    for (opcode=1U; opcode<=4U; ++opcode) {
        for (token=0U; token<256U; ++token) {
            request=Query((uint8_t)opcode,(uint8_t)token);
            memcpy(before,&request,sizeof(request));
            expected[2]=(uint8_t)opcode;
            expected[3]=(uint8_t)token;
            memcpy(expected+4U,value_bytes[2],4U);
            CHECK(CanLinkProtocol_QueryOpcode(&request) == opcode);
            CHECK(CanLinkProtocol_MakeQueryReply(&request,values[2],&reply) == 1);
            CheckQueryReply(&reply,expected);
            CHECK(memcmp(before,&request,sizeof(request)) == 0);
            /* A query is not accidentally accepted as a legacy PING. */
            ExpectRejected(&request);
        }
        for (v=0U; v<sizeof(values)/sizeof(values[0]); ++v) {
            request=Query((uint8_t)opcode,0xA5U);
            expected[2]=(uint8_t)opcode;
            expected[3]=0xA5U;
            memcpy(expected+4U,value_bytes[v],4U);
            CHECK(CanLinkProtocol_MakeQueryReply(&request,values[v],&request) == 1);
            CheckQueryReply(&request,expected);
            /* Replies never trigger a request/reply loop. */
            RejectQuery(&request);
        }
    }
}

static void InvalidQueries(void)
{
    static const uint32_t ids[]={0U,0x320U,0x322U,0x7FFU,0x800U,0x1FFFFFFFU,0xFFFFFFFFU};
    static const uint8_t flags[]={1U,2U,255U};
    unsigned i, bit, byte;
    CanLinkFrame request;
    unsigned char before[sizeof(request)];
    for (i=0U;i<sizeof(ids)/sizeof(ids[0]);++i) {
        request=Query(1U,0U); request.id=ids[i]; RejectQuery(&request);
    }
    for (i=0U;i<sizeof(flags)/sizeof(flags[0]);++i) {
        request=Query(1U,0U); request.extended=flags[i]; RejectQuery(&request);
        request=Query(1U,0U); request.remote=flags[i]; RejectQuery(&request);
        request=Query(1U,0U); request.fd=flags[i]; RejectQuery(&request);
        request=Query(1U,0U); request.brs=flags[i]; RejectQuery(&request);
    }
    for (i=0U;i<256U;++i) {
        if (i != 8U) {
            request=Query(1U,0U); request.length=(uint8_t)i; RejectQuery(&request);
        }
        if (i != 1U) {
            request=Query(1U,0U); request.data[1]=(uint8_t)i; RejectQuery(&request);
        }
        if (i < 1U || i > 4U) {
            request=Query((uint8_t)i,0U); RejectQuery(&request);
        }
        if (i != 0x51U) {
            request=Query(1U,0U); request.data[0]=(uint8_t)i; RejectQuery(&request);
        }
    }
    for (byte=4U;byte<8U;++byte) {
        for (bit=0U;bit<8U;++bit) {
            request=Query(1U,0xFFU);
            request.data[byte]=(uint8_t)(1U<<bit);
            RejectQuery(&request);
        }
    }
    request=Ping(); RejectQuery(&request);
    RejectQuery(NULL);
    request=Query(1U,0U);
    CHECK(CanLinkProtocol_MakeQueryReply(&request,0U,NULL) == 0);
    CHECK(CanLinkProtocol_MakeQueryReply(NULL,0U,NULL) == 0);
    request.data[7]=1U;
    memcpy(before,&request,sizeof(request));
    CHECK(CanLinkProtocol_MakeQueryReply(&request,0U,&request) == 0);
    CHECK(memcmp(before,&request,sizeof(request)) == 0);
}

int main(void)
{
    ValidFrames();
    RejectedFrames();
    ValidQueries();
    InvalidQueries();
    printf("CAN protocol HOST PASS: %u assertions\n", checks);
    return EXIT_SUCCESS;
}
