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
    CHECK(CAN_QUERY_BOOT_STATE == 5U && CAN_QUERY_BOOT_WORD0 == 6U);
    CHECK(CAN_QUERY_BOOT_WORD1 == 7U && CAN_QUERY_BOOT_WORD2 == 8U && CAN_QUERY_BOOT_WORD3 == 9U);
    CHECK(CAN_BOOT_STATE_NOT_READY == 0U && CAN_BOOT_STATE_VALID == 1U && CAN_BOOT_STATE_FAILED == 2U);
    CHECK(CAN_QUERY_TIME_STATE == 10U && CAN_QUERY_RX_AGE_US == 11U);
    CHECK(CAN_QUERY_EPOCH == 12U);
    CHECK(CAN_QUERY_INFO_VALUE == 0x000103FFU);
    CHECK(CAN_QUERY_BUILD_REVISION == 0x00000817U);
    for (opcode=1U; opcode<=12U; ++opcode) {
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
        if (i < 1U || i > 12U) {
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


static void InvalidBootQueries(void)
{
    unsigned opcode, i, byte;
    CanLinkFrame request;
    for (opcode=5U;opcode<=12U;++opcode) {
        for (i=0U;i<16U;++i) {
            request=Query((uint8_t)opcode,0xFFU);
            switch(i) {
            case 0: request.id=0x322U; break;
            case 1: request.extended=2U; break;
            case 2: request.remote=255U; break;
            case 3: request.fd=1U; break;
            case 4: request.brs=255U; break;
            case 5: request.data[0]=0x52U; break;
            case 6: request.data[1]=0U; break;
            case 7: request.data[1]=2U; break;
            default: request.length=(uint8_t)(i+1U); break;
            }
            RejectQuery(&request);
        }
        for (byte=4U;byte<8U;++byte) {
            for (i=0U;i<8U;++i) {
                request=Query((uint8_t)opcode,0U);
                request.data[byte]=(uint8_t)(1U<<i);
                RejectQuery(&request);
            }
        }
    }
}

static CanLinkFrame Stop(uint32_t id)
{
    CanLinkFrame request = Ping();
    unsigned i;
    request.data[0]=0x43U; request.data[1]=1U;
    request.data[2]=0x10U; request.data[3]=0U;
    for(i=0U;i<4U;++i) request.data[i+4U]=(uint8_t)(id>>(8U*i));
    return request;
}
static void RejectStop(const CanLinkFrame *request)
{
    CanLinkFrame reply;
    unsigned char before[sizeof(reply)];
    memset(&reply,0xD3,sizeof(reply)); memcpy(before,&reply,sizeof(reply));
    CHECK(CanLinkProtocol_StopRequestId(request)==0U);
    CHECK(CanLinkProtocol_MakeStopReply(request,1U,&reply)==0);
    CHECK(memcmp(before,&reply,sizeof(reply))==0);
}
static void StopFrames(void)
{
    static const uint32_t ids[]={1U,0x12345678U,0x80000000U,0xFFFFFFFFU};
    static const uint8_t id_bytes[][4]={
        {1U,0U,0U,0U},{0x78U,0x56U,0x34U,0x12U},
        {0U,0U,0U,0x80U},{0xFFU,0xFFU,0xFFU,0xFFU}};
    CanLinkFrame request,reply;
    unsigned i,status,byte,bit;
    unsigned char before[sizeof(request)];
    CHECK(CAN_STOP_OPCODE==0x10U && CAN_STOP_ACCEPTED==1U);
    CHECK(CAN_STOP_PROCESSED==2U && CAN_STOP_STALE==3U);
    CHECK(CAN_STOP_BUSY==4U && CAN_STOP_REJECTED==5U);
    for(i=0U;i<sizeof(ids)/sizeof(ids[0]);++i) {
        for(status=1U;status<=5U;++status) {
            uint8_t expected[8]={0x41U,1U,0x10U,0U,0U,0U,0U,0U};
            request=Stop(ids[i]); memcpy(before,&request,sizeof(request));
            expected[3]=(uint8_t)status; memcpy(expected+4U,id_bytes[i],4U);
            CHECK(CanLinkProtocol_StopRequestId(&request)==ids[i]);
            CHECK(CanLinkProtocol_MakeStopReply(&request,(uint8_t)status,&reply)==1);
            CheckQueryReply(&reply,expected);
            CHECK(memcmp(before,&request,sizeof(request))==0);
            ExpectRejected(&request); RejectQuery(&request);
            CHECK(CanLinkProtocol_MakeStopReply(&request,(uint8_t)status,&request)==1);
            CheckQueryReply(&request,expected);
            RejectStop(&request); /* STOP ACK cannot trigger another STOP. */
        }
    }
    for(i=0U;i<256U;++i) {
        if(i!=8U) { request=Stop(1U); request.length=(uint8_t)i; RejectStop(&request); }
        if(i<1U || i>5U) {
            request=Stop(1U); memcpy(before,&request,sizeof(request));
            CHECK(CanLinkProtocol_MakeStopReply(&request,(uint8_t)i,&request)==0);
            CHECK(memcmp(before,&request,sizeof(request))==0);
        }
    }
    for(byte=0U;byte<4U;++byte) {
        for(bit=0U;bit<8U;++bit) {
            request=Stop(1U); request.data[byte]^=(uint8_t)(1U<<bit); RejectStop(&request);
        }
    }
    for(i=1U;i<256U;++i) {
        request=Stop(1U); request.extended=(uint8_t)i; RejectStop(&request);
        request=Stop(1U); request.remote=(uint8_t)i; RejectStop(&request);
        request=Stop(1U); request.fd=(uint8_t)i; RejectStop(&request);
        request=Stop(1U); request.brs=(uint8_t)i; RejectStop(&request);
    }
    for(i=0U;i<0x800U;++i) {
        if(i==0x321U) continue;
        request=Stop(1U); request.id=i; RejectStop(&request);
    }
    request=Stop(1U); request.id=0xFFFFFFFFU; RejectStop(&request);
    request=Stop(0U); RejectStop(&request);
    memcpy(before,&request,sizeof(request));
    CHECK(CanLinkProtocol_MakeStopReply(&request,1U,&request)==0);
    CHECK(memcmp(before,&request,sizeof(request))==0);
    request=Ping(); RejectStop(&request);
    request=Query(1U,0U); RejectStop(&request);
    RejectStop(NULL);
    request=Stop(1U);
    CHECK(CanLinkProtocol_MakeStopReply(&request,1U,NULL)==0);
    CHECK(CanLinkProtocol_MakeStopReply(NULL,1U,NULL)==0);
}


static CanLinkFrame Target(uint8_t token,uint16_t speed_bits,uint16_t iq_ma)
{
    CanLinkFrame f=Ping();
    f.data[0]=0x43U; f.data[1]=1U; f.data[2]=0x20U; f.data[3]=token;
    f.data[4]=(uint8_t)speed_bits; f.data[5]=(uint8_t)(speed_bits>>8U);
    f.data[6]=(uint8_t)iq_ma; f.data[7]=(uint8_t)(iq_ma>>8U);
    return f;
}
static void RejectTarget(const CanLinkFrame *request)
{
    CanLinkTargetPair pair;
    CanLinkFrame reply;
    unsigned char before[sizeof(reply)],pair_before[sizeof(pair)];
    memset(&pair,0x59,sizeof(pair)); memcpy(pair_before,&pair,sizeof(pair));
    memset(&reply,0xD6,sizeof(reply)); memcpy(before,&reply,sizeof(reply));
    CHECK(CanLinkProtocol_ReadTargetCheck(request,&pair)==0);
    CHECK(memcmp(&pair,pair_before,sizeof(pair))==0);
    CHECK(CanLinkProtocol_MakeTargetCheckReply(request,1U,&reply)==0);
    CHECK(memcmp(&reply,before,sizeof(reply))==0);
}
static void TargetFrames(void)
{
    static const uint16_t speed_bits[]={0U,1U,1000U,0x7FFFU,0x8000U,0xFC18U,0xFF9CU,0xFFFFU};
    static const int32_t speeds[]={0,1,1000,32767,-32768,-1000,-100,-1};
    static const uint16_t currents[]={0U,1U,500U,4000U,4001U,0xFFFFU};
    CanLinkFrame request,reply;
    CanLinkTargetPair pair;
    unsigned i,j,token,status,field;
    unsigned char before[sizeof(request)];
    CHECK(CAN_TARGET_CHECK_OPCODE==0x20U);
    CHECK(CAN_TARGET_CHECK_OK==1U && CAN_TARGET_CHECK_RANGE==2U);
    CHECK(CAN_TARGET_CHECK_CONFIG==3U && CAN_TARGET_CHECK_REJECTED==4U);
    for(i=0U;i<sizeof(speeds)/sizeof(speeds[0]);++i) {
        for(j=0U;j<sizeof(currents)/sizeof(currents[0]);++j) {
            request=Target(0xA5U,speed_bits[i],currents[j]);
            memcpy(before,&request,sizeof(request));
            CHECK(CanLinkProtocol_ReadTargetCheck(&request,&pair)==1);
            CHECK(pair.speed_rpm==speeds[i] && pair.iq_limit_ma==currents[j]);
            CHECK(memcmp(before,&request,sizeof(request))==0);
            /* Range validation belongs to the provider, not this codec. */
            ExpectRejected(&request); RejectQuery(&request); RejectStop(&request);
        }
    }
    /* Every token and response status; reply echoes the exact whole pair.
     * Different signed/unsigned boundary encodings also survive in-place use. */
    for(token=0U;token<256U;++token) for(status=1U;status<=4U;++status) {
        uint8_t expected[8]={0x56U,1U,0U,0U,0x9CU,0xFFU,0xF4U,1U};
        expected[2]=(uint8_t)status; expected[3]=(uint8_t)token;
        request=Target((uint8_t)token,0xFF9CU,500U);
        memcpy(before,&request,sizeof(request));
        CHECK(CanLinkProtocol_MakeTargetCheckReply(&request,(uint8_t)status,&reply)==1);
        CheckQueryReply(&reply,expected);
        CHECK(memcmp(before,&request,sizeof(request))==0);
        CHECK(CanLinkProtocol_MakeTargetCheckReply(&request,(uint8_t)status,&request)==1);
        CheckQueryReply(&request,expected);
        RejectTarget(&request); /* Returned results must not become requests. */
    }
    for(i=0U;i<256U;++i) {
        request=Target(0U,0U,0U);
        if(i!=8U) {request.length=(uint8_t)i;RejectTarget(&request);}
        for(field=0U;field<3U;++field) {
            uint8_t correct=(field==0U)?0x43U:(field==1U)?1U:0x20U;
            if(i==correct)continue;
            request=Target(0U,0U,0U);request.data[field]=(uint8_t)i;RejectTarget(&request);
        }
        if(i<1U || i>4U) {
            request=Target(0U,1U,1U);memcpy(before,&request,sizeof(request));
            CHECK(CanLinkProtocol_MakeTargetCheckReply(&request,(uint8_t)i,&request)==0);
            CHECK(memcmp(before,&request,sizeof(request))==0);
        }
        if(i) {
            request=Target(0U,0U,0U);request.extended=(uint8_t)i;RejectTarget(&request);
            request=Target(0U,0U,0U);request.remote=(uint8_t)i;RejectTarget(&request);
            request=Target(0U,0U,0U);request.fd=(uint8_t)i;RejectTarget(&request);
            request=Target(0U,0U,0U);request.brs=(uint8_t)i;RejectTarget(&request);
        }
    }
    for(i=0U;i<0x800U;++i)if(i!=0x321U) {
        request=Target(0U,0U,0U);request.id=i;RejectTarget(&request);
    }
    request=Target(0U,0U,0U);request.id=0xFFFFFFFFU;RejectTarget(&request);
    request=Target(0U,0U,0U);
    CHECK(CanLinkProtocol_ReadTargetCheck(&request,NULL)==0);
    CHECK(CanLinkProtocol_MakeTargetCheckReply(&request,1U,NULL)==0);
    CHECK(CanLinkProtocol_MakeTargetCheckReply(NULL,1U,NULL)==0);
    RejectTarget(NULL);
    request=Ping();RejectTarget(&request);
    request=Query(1U,0U);RejectTarget(&request);
    request=Stop(1U);RejectTarget(&request);
}

static CanLinkFrame TransactionFrame(uint8_t page,uint8_t token,uint32_t value)
{
    CanLinkFrame f=Ping();unsigned i;
    f.data[0]=0x43U;f.data[1]=2U;f.data[2]=page;f.data[3]=token;
    for(i=0U;i<4U;++i)f.data[4U+i]=(uint8_t)(value>>(8U*i));
    return f;
}
static void RejectTransaction(const CanLinkFrame *request)
{
    CanLinkTransactionFrame decoded;unsigned char before[sizeof(decoded)];
    memset(&decoded,0xA5,sizeof(decoded));memcpy(before,&decoded,sizeof(decoded));
    CHECK(CanLinkProtocol_ReadTransaction(request,&decoded)==0);
    CHECK(memcmp(before,&decoded,sizeof(decoded))==0);
}
static void TransactionFrames(void)
{
    static const uint32_t values[]={0U,1U,0x80000000U,0x89ABCDEFU,UINT32_MAX};
    static const uint32_t words[4][8]={
        {1U,1U,2U,3U,4U,1U,0x01F40064U,0x03E80001U},
        {0U,0U,0U,0U,0U,0U,0U,0U},
        {UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX},
        {0U,1U,2U,3U,4U,5U,6U,7U}};
    /* Independently calculated with Python zlib over struct.pack('<8I',...). */
    static const uint32_t crc[]={0x80B3BFF2U,0x190A55ADU,0xFF6CAB0BU,0x790723DCU};
    CanLinkFrame frame,reply;CanLinkTransactionFrame decoded;
    unsigned char before[sizeof(frame)];uint32_t mutated[8];
    unsigned page,token,i,field,phase,detail,valid;
    CHECK(CAN_TXN_PAGE_BEGIN==0x30U && CAN_TXN_PAGE_BOOT0==0x31U && CAN_TXN_PAGE_BOOT3==0x34U);
    CHECK(CAN_TXN_PAGE_EPOCH==0x35U && CAN_TXN_PAGE_TARGETS==0x36U && CAN_TXN_PAGE_META==0x37U);
    CHECK(CAN_TXN_PAGE_COMMIT==0x38U && CAN_TXN_PAGE_RESULT==0x39U);
    CHECK(CAN_TXN_STAGING==1U && CAN_TXN_ACCEPTED==2U && CAN_TXN_CORE_RESULT==3U);
    CHECK(CAN_TXN_REJECTED==4U && CAN_TXN_BUSY==5U && CAN_TXN_STALE==6U && CAN_TXN_UNKNOWN==7U);
    CHECK(CAN_TXN_MAX_TTL_MS==1000U && CAN_TXN_MAX_TICKS==31250U && CAN_TXN_CORE_RESULT_MAX==16U);
    for(page=0x30U;page<=0x39U;++page) {
        for(token=0U;token<256U;++token) {
            frame=TransactionFrame((uint8_t)page,(uint8_t)token,0x89ABCDEFU);
            memcpy(before,&frame,sizeof(frame));
            CHECK(CanLinkProtocol_ReadTransaction(&frame,&decoded)==1);
            CHECK(decoded.page==page && decoded.token==token && decoded.value==0x89ABCDEFU);
            CHECK(memcmp(before,&frame,sizeof(frame))==0);
            CHECK(CanLinkProtocol_QueryOpcode(&frame)==0U && CanLinkProtocol_StopRequestId(&frame)==0U);
            ExpectRejected(&frame);RejectTarget(&frame);
        }
        for(i=0U;i<sizeof(values)/sizeof(values[0]);++i) {
            frame=TransactionFrame((uint8_t)page,0U,values[i]);
            if(page==0x30U && values[i]==0U)RejectTransaction(&frame);
            else {
                CHECK(CanLinkProtocol_ReadTransaction(&frame,&decoded)==1);
                CHECK(decoded.value==values[i]);
            }
        }
    }
    for(i=0U;i<256U;++i) {
        frame=TransactionFrame(0x30U,0U,1U);
        if(i!=8U) { frame.length=(uint8_t)i;RejectTransaction(&frame); }
        for(field=0U;field<3U;++field) {
            unsigned expected=field==0U?0x43U:field==1U?2U:0x30U;
            if((field==2U && i>=0x30U && i<=0x39U) || i==expected)continue;
            frame=TransactionFrame(0x30U,0U,1U);frame.data[field]=(uint8_t)i;RejectTransaction(&frame);
        }
        if(i) {
            frame=TransactionFrame(0x31U,0U,0U);frame.extended=(uint8_t)i;RejectTransaction(&frame);
            frame=TransactionFrame(0x31U,0U,0U);frame.remote=(uint8_t)i;RejectTransaction(&frame);
            frame=TransactionFrame(0x31U,0U,0U);frame.fd=(uint8_t)i;RejectTransaction(&frame);
            frame=TransactionFrame(0x31U,0U,0U);frame.brs=(uint8_t)i;RejectTransaction(&frame);
        }
    }
    for(i=0U;i<0x800U;++i)if(i!=0x321U) {
        frame=TransactionFrame(0x39U,0U,0U);frame.id=i;RejectTransaction(&frame);
    }
    frame=TransactionFrame(0x30U,0U,1U);frame.id=UINT32_MAX;RejectTransaction(&frame);
    frame=TransactionFrame(0x39U,0U,0U);CHECK(CanLinkProtocol_ReadTransaction(&frame,NULL)==0);
    RejectTransaction(NULL);frame=Ping();RejectTransaction(&frame);
    frame=Query(12U,0U);RejectTransaction(&frame);frame=Stop(1U);RejectTransaction(&frame);
    frame=Target(0U,1U,2U);RejectTransaction(&frame);
    for(phase=0U;phase<=8U;++phase)for(detail=0U;detail<256U;++detail) {
        valid=(phase==1U && detail>=0x31U && detail<=0x38U) ||
              (phase==3U && detail>=1U && detail<=16U) ||
              (phase==4U && detail>=1U && detail<=11U) ||
              ((phase==2U || (phase>=5U && phase<=7U)) && detail==0U);
        memset(&reply,0xA5,sizeof(reply));memcpy(before,&reply,sizeof(reply));
        CHECK(CanLinkProtocol_MakeTransactionReply(0x89ABCDEFU,(uint8_t)phase,(uint8_t)detail,&reply)==(int)valid);
        if(valid) {
            CHECK(reply.id==0x322U && reply.length==8U && reply.extended==0U && reply.remote==0U && reply.fd==0U && reply.brs==0U);
            CHECK(reply.data[0]==0x41U && reply.data[1]==2U && reply.data[2]==phase && reply.data[3]==detail);
            CHECK(reply.data[4]==0xEFU && reply.data[5]==0xCDU && reply.data[6]==0xABU && reply.data[7]==0x89U);
            RejectTransaction(&reply);RejectQuery(&reply);
        } else CHECK(memcmp(before,&reply,sizeof(reply))==0);
        memset(&reply,0x5A,sizeof(reply));memcpy(before,&reply,sizeof(reply));
        CHECK(CanLinkProtocol_MakeTransactionReply(0U,(uint8_t)phase,(uint8_t)detail,&reply)==(int)(phase==7U && detail==0U));
        if(phase!=7U || detail!=0U)CHECK(memcmp(before,&reply,sizeof(reply))==0);
    }
    CHECK(CanLinkProtocol_MakeTransactionReply(1U,2U,0U,NULL)==0);
    CHECK(CanLinkProtocol_TransactionCrc32(NULL)==0U);
    for(i=0U;i<4U;++i)CHECK(CanLinkProtocol_TransactionCrc32(words[i])==crc[i]);
    for(field=0U;field<8U;++field)for(i=0U;i<32U;++i) {
        memcpy(mutated,words[0],sizeof(mutated));mutated[field]^=1U<<i;
        CHECK(CanLinkProtocol_TransactionCrc32(mutated)!=crc[0]);
    }
}

int main(void)
{
    ValidFrames();
    RejectedFrames();
    ValidQueries();
    InvalidQueries();
    InvalidBootQueries();
    StopFrames();
    TargetFrames();
    TransactionFrames();
    printf("CAN protocol HOST PASS: %u assertions\n", checks);
    return EXIT_SUCCESS;
}
