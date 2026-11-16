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

int main(void)
{
    ValidFrames();
    RejectedFrames();
    printf("CAN protocol HOST PASS: %u assertions\n", checks);
    return EXIT_SUCCESS;
}
