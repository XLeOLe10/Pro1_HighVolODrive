/* Tests compile the real service and protocol; no product test hooks.
 * Each scenario is a fresh process, preserving one-shot initialization.
 * Register writes and HAL behavior are deterministic mocks, not silicon timing.
 * STOP scenarios additionally link the real pure command core through callbacks.
 * No PWM/BoardSafety/port symbols are supplied: direct hardware control fails link.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdcan.h"
#include "can_link.h"
#include "can_link_protocol.h"
#include "axis_command_core.h"

FDCAN_GlobalTypeDef host_can_registers;
uint32_t host_message_ram[161];
FDCAN_HandleTypeDef hfdcan1;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned config_calls, global_calls, blocking_calls, start_calls;
static unsigned receive_calls, send_calls, fill_calls, free_calls, clear_calls;
static unsigned fail_init_at;
static uint32_t fifo_fill, fifo_free = 32U, cleared_flags;
static HAL_StatusTypeDef receive_result = HAL_OK, send_result = HAL_OK;
static FDCAN_RxHeaderTypeDef incoming;
static uint8_t incoming_data[8];
static FDCAN_TxHeaderTypeDef sent_header;
static uint8_t sent_data[8];
static unsigned status_reads, ignored_status_reads;
static CanLinkReadStatus observed_status;
static unsigned stop_calls, ignored_stop_calls;
static uint32_t stop_success=1U, stop_ticket=41U;
static unsigned use_core;
static AxisCommandCore real_core;
static uint32_t RequestStop(uint32_t *ticket)
{
    CHECK(ticket!=NULL);
    ++stop_calls;
    if(use_core) return AxisCommand_RequestStop(&real_core,ticket)==AXIS_CMD_OK ? 1U : 0U;
    *ticket=stop_ticket;
    return stop_success;
}
static uint32_t IgnoredStop(uint32_t *ticket)
{
    (void)ticket; ++ignored_stop_calls;
    return 0U;
}
static void ReadStatus(CanLinkReadStatus *out)
{
    CHECK(out != NULL);
    ++status_reads;
    *out = observed_status;
    if(use_core) out->stop_applied_ticket=real_core.stop_applied_ticket;
}
static void IgnoredStatus(CanLinkReadStatus *out)
{
    (void)out;
    ++ignored_status_reads;
}


void HostClearFlag(FDCAN_HandleTypeDef *h, uint32_t mask)
{
    CHECK(h == &hfdcan1);
    ++clear_calls;
    cleared_flags = mask;
    /* Emulate W1C, preserving any unrelated pending flags. */
    h->Instance->IR &= ~mask;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *h, const FDCAN_FilterTypeDef *f)
{
    CHECK(h == &hfdcan1);
    ++config_calls;
    CHECK(f->IdType == FDCAN_STANDARD_ID && f->FilterIndex == 0U);
    CHECK(f->FilterType == FDCAN_FILTER_MASK && f->FilterConfig == FDCAN_FILTER_TO_RXFIFO0);
    CHECK(f->FilterID1 == 0x321U && f->FilterID2 == 0x7FFU);
    return fail_init_at == 1U ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    CHECK(h == &hfdcan1);
    ++global_calls;
    CHECK(a == FDCAN_REJECT && b == FDCAN_REJECT);
    CHECK(c == FDCAN_REJECT_REMOTE && d == FDCAN_REJECT_REMOTE);
    return fail_init_at == 2U ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigRxFifoOverwrite(FDCAN_HandleTypeDef *h, uint32_t fifo, uint32_t mode)
{
    CHECK(h == &hfdcan1 && fifo == FDCAN_RX_FIFO0);
    ++blocking_calls;
    CHECK(mode == FDCAN_RX_FIFO_BLOCKING);
    if (fail_init_at == 3U) return HAL_ERROR;
    h->Instance->RXF0C &= ~FDCAN_RXF0C_F0OM;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h)
{
    CHECK(h == &hfdcan1);
    ++start_calls;
    if (fail_init_at == 4U) return HAL_ERROR;
    h->State = HAL_FDCAN_STATE_BUSY;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *h, uint32_t fifo)
{
    CHECK(h == &hfdcan1 && fifo == FDCAN_RX_FIFO0);
    ++fill_calls;
    return fifo_fill;
}
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t fifo, FDCAN_RxHeaderTypeDef *out, uint8_t *data)
{
    uint32_t index = (h->Instance->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    CHECK(h == &hfdcan1 && fifo == FDCAN_RX_FIFO0 && fifo_fill != 0U);
    ++receive_calls;
    /* Receiving an unsafe DLC here fails the test before any copy. */
    CHECK(((host_message_ram[2U + index * 4U] >> 16U) & 15U) == 8U);
    if (receive_result != HAL_OK) return receive_result;
    *out = incoming;
    memcpy(data, incoming_data, sizeof(incoming_data));
    --fifo_fill;
    h->Instance->RXF0A = index;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h)
{
    CHECK(h == &hfdcan1);
    ++free_calls;
    return fifo_free;
}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h, const FDCAN_TxHeaderTypeDef *header, const uint8_t *data)
{
    CHECK(h == &hfdcan1 && fifo_free != 0U);
    ++send_calls;
    CHECK(header->Identifier == 0x322U && header->IdType == FDCAN_STANDARD_ID);
    CHECK(header->TxFrameType == FDCAN_DATA_FRAME && header->DataLength == FDCAN_DLC_BYTES_8);
    CHECK(header->ErrorStateIndicator == FDCAN_ESI_ACTIVE && header->BitRateSwitch == FDCAN_BRS_OFF);
    CHECK(header->FDFormat == FDCAN_CLASSIC_CAN && header->TxEventFifoControl == FDCAN_NO_TX_EVENTS);
    sent_header = *header;
    memcpy(sent_data, data, sizeof(sent_data));
    if (send_result != HAL_OK) return send_result;
    --fifo_free;
    return HAL_OK;
}
static void Setup(void)
{
    hfdcan1.Instance = FDCAN1;
    hfdcan1.State = HAL_FDCAN_STATE_READY;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = DISABLE;
    hfdcan1.Init.StdFiltersNbr = 1U;
    hfdcan1.Init.RxFifo0ElmtsNbr = 8U;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 32U;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.msgRam.RxFIFO0SA = SRAMCAN_BASE + 4U;
    hfdcan1.msgRam.EndAddress = SRAMCAN_BASE + 644U;
}
static void Ready(void)
{
    CanLink_Init(ReadStatus,RequestStop);
    CHECK(can_link_diag.state == 1U && can_link_error_count == 0U);
    CHECK(config_calls == 1U && global_calls == 1U && blocking_calls == 1U && start_calls == 1U);
    CanLink_Init(IgnoredStatus,IgnoredStop);
    CHECK(config_calls == 1U && start_calls == 1U);
}
static void Queue(uint32_t dlc, uint32_t index, uint32_t count)
{
    static const uint8_t ping[8] = {0x50U, 0x49U, 0x4EU, 0x47U, 0x00U, 0xA5U, 0xFFU, 0x80U};
    memset(&incoming, 0, sizeof(incoming));
    incoming.Identifier = 0x321U;
    incoming.DataLength = FDCAN_DLC_BYTES_8;
    memcpy(incoming_data, ping, sizeof(ping));
    fifo_fill = count;
    host_can_registers.RXF0S = index << FDCAN_RXF0S_F0GI_Pos;
    host_can_registers.RXF0A = 0xFFFFFFFFU;
    if (index < 8U) host_message_ram[2U + index * 4U] = dlc << 16U;
}
static void Valid(void)
{
    static const uint8_t pong[8] = {0x50U, 0x4FU, 0x4EU, 0x47U, 0x00U, 0xA5U, 0xFFU, 0x80U};
    Ready();
    Queue(8U, 7U, 8U);
    CanLink_Poll();
    CHECK(receive_calls == 1U && send_calls == 1U);
    CHECK(fifo_fill == 7U && fifo_free == 31U); /* no drain-all loop */
    CHECK(can_link_rx_count == 1U && can_link_diag.tx_enqueued_count == 1U);
    CHECK(can_link_error_count == 0U && can_link_diag.rejected_count == 0U);
    CHECK(host_can_registers.RXF0A == 7U && memcmp(sent_data, pong, 8U) == 0);
    CHECK(sent_header.MessageMarker == 0U);
    fifo_fill = 0U;
    CanLink_Poll();
    CHECK(receive_calls == 1U && send_calls == 1U); /* no periodic duplicate */
    CHECK(free_calls == 1U);
    CHECK(status_reads == 0U && ignored_status_reads == 0U && can_query_rx_count == 0U);
}
static void Lengths(void)
{
    uint32_t dlc, expected_rejects = 0U;
    Ready();
    for (dlc = 0U; dlc < 16U; ++dlc) {
        unsigned rx_before = receive_calls, tx_before = send_calls;
        Queue(dlc, dlc % 8U, 1U);
        CanLink_Poll();
        if (dlc == 8U) {
            CHECK(receive_calls == rx_before + 1U && send_calls == tx_before + 1U);
        } else {
            ++expected_rejects;
            CHECK(receive_calls == rx_before && send_calls == tx_before);
            CHECK(host_can_registers.RXF0A == dlc % 8U);
        }
        CHECK(can_link_diag.rejected_count == expected_rejects);
    }
    CHECK(can_link_rx_count == 1U && can_link_error_count == 0U);
}
static void Rejects(void)
{
    uint32_t i;
    Ready();
    for (i = 0U; i < 7U; ++i) {
        Queue(8U, 0U, 1U);
        switch (i) {
        case 0: incoming.Identifier = 0x322U; break;
        case 1: incoming.IdType = 1U; break;
        case 2: incoming.RxFrameType = 1U; break;
        case 3: incoming.FDFormat = 1U; break;
        case 4: incoming.BitRateSwitch = 1U; break;
        case 5: incoming.DataLength = 7U; break;
        default: incoming_data[0] = 0U; break;
        }
        CanLink_Poll();
        CHECK(can_link_diag.rejected_count == i + 1U);
        CHECK(can_link_rx_count == 0U && send_calls == 0U && free_calls == 0U);
    }
    CHECK(can_link_error_count == 0U);
}
static void Busy(void)
{
    Ready();
    fifo_free = 0U;
    Queue(8U, 0U, 1U);
    CanLink_Poll();
    CHECK(can_link_rx_count == 1U && send_calls == 0U);
    CHECK(can_link_diag.tx_busy_count == 1U && can_link_error_count == 1U);
    CHECK(can_link_diag.error_flags == CAN_LINK_ERROR_TX_BUSY);
    fifo_free = 32U;
    CanLink_Poll();
    CHECK(send_calls == 0U && can_link_diag.tx_enqueued_count == 0U); /* no delayed retry */
}
static void HalFailure(int tx)
{
    Ready();
    Queue(8U, 0U, 1U);
    if (tx) send_result = HAL_ERROR; else receive_result = HAL_ERROR;
    CanLink_Poll();
    CHECK(receive_calls == 1U && send_calls == (unsigned)tx);
    CHECK(can_link_rx_count == (uint32_t)tx && can_link_diag.tx_enqueued_count == 0U);
    CHECK(can_link_error_count == 1U && can_link_diag.error_flags == CAN_LINK_ERROR_HAL);
}
static void Errors(void)
{
    uint32_t mask = FDCAN_IR_RF0L | FDCAN_IR_PEA | FDCAN_IR_RF0N | FDCAN_IR_TC;
    Ready();
    host_can_registers.IR = mask | 0x80000000U; /* unrelated flag must survive */
    host_can_registers.PSR = 3U;
    host_can_registers.ECR = (37U << FDCAN_ECR_TEC_Pos) | (19U << FDCAN_ECR_REC_Pos);
    CanLink_Poll();
    CHECK(cleared_flags == mask && host_can_registers.IR == 0x80000000U);
    CHECK(clear_calls == 1U && can_link_diag.last_ir == mask);
    CHECK(can_link_diag.last_lec == 3U && can_link_diag.tx_error_count == 37U && can_link_diag.rx_error_count == 19U);
    CHECK(can_link_diag.rx_lost_events == 1U && can_link_diag.protocol_error_events == 1U);
    CHECK(can_link_error_count == 1U); /* coalesced observation, not exact frame count */
    CHECK(can_link_diag.error_flags == (CAN_LINK_ERROR_RX_LOST | CAN_LINK_ERROR_PROTOCOL));
    CanLink_Poll();
    CHECK(can_link_error_count == 1U && clear_calls == 1U);
}
static void Latched(int busoff, int register_only)
{
    Ready();
    Queue(8U, 0U, 1U);
    if (busoff) {
        if (register_only) host_can_registers.PSR = FDCAN_PSR_BO;
        else host_can_registers.IR = FDCAN_IR_BO;
    } else host_can_registers.IR = FDCAN_IR_MRAF;
    CanLink_Poll();
    CHECK(can_link_diag.state == (busoff ? 3U : 2U));
    CHECK(can_link_diag.error_flags == (busoff ? CAN_LINK_ERROR_BUS_OFF : CAN_LINK_ERROR_RAM));
    CHECK(can_link_error_count == 1U && receive_calls == 0U && send_calls == 0U);
    host_can_registers.PSR = 0U; host_can_registers.IR = 0U;
    CanLink_Init(ReadStatus,RequestStop); CanLink_Poll();
    CHECK(can_link_diag.state == (busoff ? 3U : 2U));
    CHECK(start_calls == 1U && receive_calls == 0U && send_calls == 0U);
}
static void RuntimeGeometry(int overwrite)
{
    Ready();
    Queue(8U, overwrite ? 0U : 8U, 1U);
    if (overwrite) host_can_registers.RXF0C |= FDCAN_RXF0C_F0OM;
    CanLink_Poll();
    CHECK(can_link_diag.state == 2U && can_link_diag.error_flags == CAN_LINK_ERROR_CONFIG);
    CHECK(receive_calls == 0U && send_calls == 0U && host_can_registers.RXF0A == 0xFFFFFFFFU);
}
static void BadConfig(unsigned field)
{
    switch (field) {
    case 0: hfdcan1.Init.RxFifo0ElmtSize = 5U; break;
    case 1: hfdcan1.Init.AutoRetransmission = 1U; break;
    case 2: hfdcan1.msgRam.RxFIFO0SA += 4U; break;
    case 3: hfdcan1.msgRam.EndAddress -= 4U; break;
    case 4: host_can_registers.IE = 1U; break;
    case 5: host_can_registers.ILE = 1U; break;
    default: hfdcan1.Instance = NULL; break;
    }
    CanLink_Init(ReadStatus,RequestStop);
    CHECK(can_link_diag.state == 2U && can_link_diag.error_flags == CAN_LINK_ERROR_CONFIG);
    CHECK(can_link_error_count == 1U && config_calls == 0U && start_calls == 0U);
    CanLink_Init(ReadStatus,RequestStop); CanLink_Poll();
    CHECK(can_link_error_count == 1U && receive_calls == 0U && start_calls == 0U);
}
static void InitFailure(unsigned step)
{
    fail_init_at = step;
    CanLink_Init(ReadStatus,RequestStop);
    CHECK(can_link_diag.state == 2U && can_link_diag.error_flags == CAN_LINK_ERROR_INIT);
    CHECK(can_link_error_count == 1U);
    CHECK(config_calls == 1U && global_calls == (step >= 2U));
    CHECK(blocking_calls == (step >= 3U) && start_calls == (step >= 4U));
    fail_init_at = 0U;
    CanLink_Init(ReadStatus,RequestStop); CanLink_Poll();
    CHECK(config_calls == 1U && receive_calls == 0U && can_link_diag.state == 2U);
}

static void QueueQuery(uint8_t opcode, uint8_t token)
{
    static const uint8_t query[8]={0x51U,0x01U,0U,0U,0U,0U,0U,0U};
    Queue(8U,0U,1U);
    memcpy(incoming_data,query,sizeof(query));
    incoming_data[2]=opcode;
    incoming_data[3]=token;
}
static void QueryValid(void)
{
    static const uint8_t expected[][8]={
        {0x52U,0x01U,0x01U,0xA5U,0x3FU,0U,0x01U,0U},
        {0x52U,0x01U,0x02U,0xA6U,0x05U,0x19U,0xCDU,0xABU},
        {0x52U,0x01U,0x03U,0xA7U,0xEFU,0xBEU,0xADU,0xDEU},
        {0x52U,0x01U,0x04U,0xA8U,0x02U,0x08U,0U,0U}
    };
    static const unsigned reads[]={0U,1U,2U,2U};
    CanLinkReadStatus before;
    unsigned i;
    Ready();
    observed_status.status_word=0x12ABCD05U;
    observed_status.fault_mask=0xDEADBEEFU;
    observed_status.configured=7U; /* Boolean normalization, not a bitwise OR. */
    before=observed_status;
    for (i=0U;i<4U;++i) {
        QueueQuery((uint8_t)(i+1U),(uint8_t)(0xA5U+i));
        fifo_fill=8U;
        CanLink_Poll();
        CHECK(receive_calls == i+1U && send_calls == i+1U && fifo_fill == 7U);
        CHECK(memcmp(sent_data,expected[i],8U)==0);
        CHECK(can_query_rx_count == i+1U && can_link_rx_count == 0U);
        CHECK(status_reads == reads[i] && ignored_status_reads == 0U);
        CHECK(memcmp(&before,&observed_status,sizeof(before))==0);
        CHECK(sent_header.MessageMarker == 0U);
    }
    fifo_fill=0U;
    CanLink_Poll();
    CHECK(receive_calls==4U && send_calls==4U && status_reads==2U);
    CHECK(can_link_diag.tx_enqueued_count==4U && can_link_error_count==0U);
    Queue(8U,0U,1U); CanLink_Poll();
    CHECK(can_link_rx_count==1U && can_query_rx_count==4U && status_reads==2U);
}
static void QueryStatus(void)
{
    static const uint8_t flags[]={0x18U,0x19U,0x1AU,0x1CU,0x1FU};
    unsigned i;
    Ready();
    for (i=0U;i<5U;++i) {
        uint8_t expected[8]={0x52U,0x01U,0x02U,0xFFU,0x04U,0U,0x34U,0x12U};
        memset(&observed_status,0,sizeof(observed_status));
        observed_status.status_word=0x00123404U;
        observed_status.configured=(i==1U || i==4U)?0x80U:0U;
        observed_status.permit=(i==2U || i==4U)?0x80000000U:0U;
        observed_status.output_enabled=(i==3U || i==4U)?2U:0U;
        expected[5]=flags[i];
        QueueQuery(2U,0xFFU); CanLink_Poll();
        CHECK(memcmp(sent_data,expected,8U)==0);
        CHECK(status_reads==i+1U && can_query_rx_count==i+1U);
    }
    CHECK(ignored_status_reads==0U);
}
static void QueryRejects(void)
{
    unsigned i;
    Ready();
    for (i=0U;i<16U;++i) {
        QueueQuery(2U,0U);
        switch(i) {
        case 0: incoming_data[0]=0x52U; break; /* Response marker */
        case 1: incoming_data[1]=0U; break;
        case 2: incoming_data[1]=2U; break;
        case 3: incoming_data[2]=0U; break;
        case 4: incoming_data[2]=5U; break; /* No hidden control opcode */
        case 5: incoming_data[2]=0xFFU; break;
        case 6: incoming_data[4]=1U; break;
        case 7: incoming_data[5]=2U; break;
        case 8: incoming_data[6]=0x80U; break;
        case 9: incoming_data[7]=0xFFU; break;
        case 10: incoming.Identifier=0x322U; break;
        case 11: incoming.IdType=1U; break;
        case 12: incoming.RxFrameType=1U; break;
        case 13: incoming.FDFormat=1U; break;
        case 14: incoming.BitRateSwitch=1U; break;
        default: incoming.DataLength=7U; break;
        }
        CanLink_Poll();
        CHECK(can_link_diag.rejected_count==i+1U);
        CHECK(send_calls==0U && free_calls==0U && status_reads==0U);
        CHECK(can_query_rx_count==0U && can_link_rx_count==0U);
    }
}
static void QueryDlc(void)
{
    unsigned dlc;
    Ready();
    for(dlc=0U;dlc<16U;++dlc) {
        unsigned r=receive_calls, t=send_calls, p=status_reads;
        QueueQuery(2U,0U);
        host_message_ram[2U]=dlc<<16U;
        CanLink_Poll();
        if(dlc==8U) {
            CHECK(receive_calls==r+1U && send_calls==t+1U && status_reads==p+1U);
        } else {
            CHECK(receive_calls==r && send_calls==t && status_reads==p);
            CHECK(host_can_registers.RXF0A==0U);
        }
    }
    CHECK(can_link_diag.rejected_count==15U && can_query_rx_count==1U);
    CHECK(can_link_rx_count==0U);
}
static void QueryBusy(void)
{
    Ready();
    fifo_free=0U;
    QueueQuery(2U,0U); CanLink_Poll();
    CHECK(can_query_rx_count==1U && can_link_rx_count==0U && send_calls==0U);
    CHECK(can_link_diag.tx_busy_count==1U && can_link_error_count==1U);
    CHECK(can_link_diag.error_flags==CAN_LINK_ERROR_TX_BUSY);
    fifo_free=32U; fifo_fill=0U;
    CanLink_Poll();
    CHECK(send_calls==0U && can_link_diag.tx_enqueued_count==0U);
    CHECK(status_reads<=1U); /* No retry/periodic sampling while idle. */
}
static void QueryHalFailure(int tx)
{
    Ready();
    QueueQuery(2U,0U);
    if(tx) send_result=HAL_ERROR; else receive_result=HAL_ERROR;
    CanLink_Poll();
    CHECK(receive_calls==1U && send_calls==(unsigned)tx);
    CHECK(status_reads==(unsigned)tx);
    CHECK(can_query_rx_count==(uint32_t)tx && can_link_rx_count==0U);
    CHECK(can_link_diag.tx_enqueued_count==0U && can_link_error_count==1U);
    CHECK(can_link_diag.error_flags==CAN_LINK_ERROR_HAL);
}
static void QueryBusOff(void)
{
    Ready();
    QueueQuery(2U,0U);
    host_can_registers.PSR=FDCAN_PSR_BO;
    CanLink_Poll();
    CHECK(can_link_diag.state==3U);
    CHECK(receive_calls==0U && send_calls==0U && status_reads==0U);
    CHECK(can_query_rx_count==0U && can_link_error_count==1U);
}
static void NoProvider(void)
{
    CanLink_Init(NULL,RequestStop);
    CHECK(can_link_diag.state==2U);
    CHECK(can_link_error_count==1U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CHECK(config_calls==0U && start_calls==0U);
    CanLink_Init(ReadStatus,RequestStop);
    QueueQuery(2U,0U); CanLink_Poll();
    CHECK(can_link_error_count==1U && can_link_diag.state==2U);
    CHECK(receive_calls==0U && send_calls==0U && status_reads==0U);
}


static void QueueStop(uint32_t id)
{
    unsigned i;
    Queue(8U,0U,1U);
    incoming_data[0]=0x43U; incoming_data[1]=1U;
    incoming_data[2]=0x10U; incoming_data[3]=0U;
    for(i=0U;i<4U;++i) incoming_data[i+4U]=(uint8_t)(id>>(8U*i));
}
static void ExpectStop(uint32_t id,uint8_t status)
{
    uint8_t expected[8]={0x41U,1U,0x10U,0U,0U,0U,0U,0U};
    unsigned i;
    expected[3]=status;
    for(i=0U;i<4U;++i) expected[i+4U]=(uint8_t)(id>>(8U*i));
    CHECK(memcmp(sent_data,expected,8U)==0);
}
static void StopValid(void)
{
    Ready();
    QueueStop(0x12345678U); fifo_fill=8U;
    CanLink_Poll();
    ExpectStop(0x12345678U,1U);
    CHECK(stop_calls==1U && receive_calls==1U && send_calls==1U && fifo_fill==7U);
    CHECK(can_link_rx_count==0U && can_query_rx_count==0U);
    /* Complete copy is not sufficient: only equality to THIS ticket is proof. */
    observed_status.stop_applied_ticket=40U;
    QueueStop(0x12345678U); CanLink_Poll(); ExpectStop(0x12345678U,1U);
    observed_status.stop_applied_ticket=42U;
    QueueStop(0x12345678U); CanLink_Poll(); ExpectStop(0x12345678U,1U);
    CHECK(stop_calls==1U);
    observed_status.stop_applied_ticket=41U;
    QueueStop(0x12345678U); CanLink_Poll(); ExpectStop(0x12345678U,2U);
    QueueStop(0x12345678U); CanLink_Poll(); ExpectStop(0x12345678U,2U);
    CHECK(stop_calls==1U && ignored_stop_calls==0U);
    CHECK(can_link_error_count==0U && can_link_diag.rejected_count==0U);
    fifo_fill=0U; CanLink_Poll();
    CHECK(send_calls==5U && stop_calls==1U);
}
static void StopOrdering(void)
{
    Ready();
    QueueStop(10U); CanLink_Poll(); ExpectStop(10U,1U);
    QueueStop(9U); CanLink_Poll(); ExpectStop(9U,3U);
    QueueStop(11U); CanLink_Poll(); ExpectStop(11U,4U);
    CHECK(stop_calls==1U);
    observed_status.stop_applied_ticket=41U;
    stop_ticket=42U;
    QueueStop(11U); CanLink_Poll(); ExpectStop(11U,1U);
    CHECK(stop_calls==2U);
    QueueStop(10U); CanLink_Poll(); ExpectStop(10U,3U);
    QueueStop(11U); CanLink_Poll(); ExpectStop(11U,1U);
    CHECK(stop_calls==2U); /* New result is not confused with prior completed STOP. */
    observed_status.stop_applied_ticket=42U;
    QueueStop(11U); CanLink_Poll(); ExpectStop(11U,2U);
}
static void StopRetry(int tx_failure)
{
    Ready();
    if(tx_failure) send_result=HAL_ERROR; else fifo_free=0U;
    QueueStop(7U); CanLink_Poll();
    CHECK(stop_calls==1U && can_link_diag.tx_enqueued_count==0U);
    CHECK(can_link_error_count==1U);
    CHECK(send_calls==(unsigned)tx_failure);
    send_result=HAL_OK; fifo_free=32U;
    fifo_fill=0U; CanLink_Poll();
    CHECK(send_calls==(unsigned)tx_failure); /* no unsolicited delayed response */
    QueueStop(7U); CanLink_Poll(); ExpectStop(7U,1U);
    CHECK(stop_calls==1U);
    observed_status.stop_applied_ticket=41U;
    QueueStop(7U); CanLink_Poll(); ExpectStop(7U,2U);
    CHECK(stop_calls==1U && can_link_diag.tx_enqueued_count==2U);
}
static void StopRejected(void)
{
    Ready();
    stop_success=0U;
    QueueStop(5U); CanLink_Poll(); ExpectStop(5U,5U);
    CHECK(stop_calls==1U);
    stop_success=1U; stop_ticket=0U;
    QueueStop(5U); CanLink_Poll(); ExpectStop(5U,5U);
    CHECK(stop_calls==2U);
    stop_ticket=41U;
    QueueStop(5U); CanLink_Poll(); ExpectStop(5U,1U);
    observed_status.stop_applied_ticket=41U;
    stop_success=0U;
    QueueStop(6U); CanLink_Poll(); ExpectStop(6U,5U);
    QueueStop(5U); CanLink_Poll(); ExpectStop(5U,2U);
    CHECK(stop_calls==4U); /* Rejected new request did not evict retained result. */
    stop_success=1U; stop_ticket=42U;
    QueueStop(6U); CanLink_Poll(); ExpectStop(6U,1U);
    CHECK(stop_calls==5U);
}
static void StopInvalid(void)
{
    unsigned i;
    Ready();
    for(i=0U;i<12U;++i) {
        QueueStop(1U);
        switch(i) {
        case 0: incoming_data[0]=0x41U; break;
        case 1: incoming_data[1]=2U; break;
        case 2: incoming_data[2]=0x11U; break;
        case 3: incoming_data[3]=1U; break;
        case 4: incoming_data[4]=0U; break; /* ID zero */
        case 5: incoming.Identifier=0x322U; break;
        case 6: incoming.IdType=1U; break;
        case 7: incoming.RxFrameType=1U; break;
        case 8: incoming.FDFormat=1U; break;
        case 9: incoming.BitRateSwitch=1U; break;
        case 10: incoming.DataLength=7U; break;
        default: incoming_data[0]=0x51U; break;
        }
        CanLink_Poll();
        CHECK(stop_calls==0U && status_reads==0U && send_calls==0U);
        CHECK(can_link_diag.rejected_count==i+1U);
    }
}
static void StopDlc(void)
{
    unsigned dlc;
    Ready();
    for(dlc=0U;dlc<16U;++dlc) {
        unsigned r=receive_calls,t=send_calls,s=stop_calls;
        QueueStop(1U); host_message_ram[2U]=dlc<<16U;
        CanLink_Poll();
        if(dlc==8U) {
            CHECK(receive_calls==r+1U && send_calls==t+1U && stop_calls==s+1U);
        } else {
            CHECK(receive_calls==r && send_calls==t && stop_calls==s);
            CHECK(host_can_registers.RXF0A==0U);
        }
    }
    CHECK(can_link_diag.rejected_count==15U && stop_calls==1U);
}
static void StopUnavailable(int busoff)
{
    Ready(); QueueStop(1U);
    if(busoff) host_can_registers.PSR=FDCAN_PSR_BO; else receive_result=HAL_ERROR;
    CanLink_Poll();
    CHECK(stop_calls==0U && send_calls==0U && status_reads==0U);
    CHECK(can_link_error_count==1U && receive_calls==(busoff?0U:1U));
}
static void NoStopProvider(void)
{
    CanLink_Init(ReadStatus,NULL);
    CHECK(can_link_diag.state==2U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CanLink_Init(ReadStatus,RequestStop);
    QueueStop(1U); CanLink_Poll();
    CHECK(can_link_error_count==1U && config_calls==0U && stop_calls==0U);
}
static void StopMaxId(void)
{
    Ready();
    QueueStop(0xFFFFFFFFU); CanLink_Poll(); ExpectStop(0xFFFFFFFFU,1U);
    observed_status.stop_applied_ticket=41U;
    QueueStop(0xFFFFFFFFU); CanLink_Poll(); ExpectStop(0xFFFFFFFFU,2U);
    QueueStop(1U); CanLink_Poll(); ExpectStop(1U,3U);
    CHECK(stop_calls==1U); /* Explicitly no request ID wrap within a boot. */
    QueueStop(0U); CanLink_Poll();
    CHECK(stop_calls==1U && send_calls==3U && can_link_diag.rejected_count==1U);
}
/* Real pure core: synthetic inputs use its public validation/publication APIs.
 * This verifies logical consumption/cancellation, not IRQ order or EN/MOE. */
static uint32_t PublishFixture(uint32_t opcode,uint32_t now)
{
    AxisCommandFrame f;
    uint32_t seq=0U;
    memset(&f,0,sizeof(f));
    f.run_epoch=real_core.run_epoch; f.opcode=opcode; f.control_mode=AXIS_CMD_SPEED_MODE;
    f.speed_target_rpm=100.0f; f.iq_limit_a=1.0f;
    f.received_tick_ms=now; f.expires_tick_ms=now+100U;
    CHECK(AxisCommand_Validate(&real_core.limits,&f,now)==AXIS_CMD_OK);
    CHECK(AxisCommand_PublishValidated(&real_core,&f,now,0U,&seq)==AXIS_CMD_OK);
    CHECK(seq!=0U);
    return seq;
}
static void StopCore(unsigned mode)
{
    AxisCommandLimits limits={1U,1000.0f,4.0f,1000U};
    uint32_t pending_seq=0U,active_seq=0U,epoch;
    if(mode==2U) limits.confirmed=0U;
    AxisCommand_Init(&real_core,&limits);
    use_core=1U;
    if(mode==0U) {
        active_seq=PublishFixture(AXIS_CMD_START,1U);
        AxisCommand_Tick(&real_core,1U,0U,1U);
        CHECK(real_core.active_valid==1U && real_core.applied_seq==active_seq);
        pending_seq=PublishFixture(AXIS_CMD_UPDATE,2U);
    } else if(mode==1U) pending_seq=PublishFixture(AXIS_CMD_START,1U);
    epoch=real_core.run_epoch;
    Ready();
    QueueStop(1U); CanLink_Poll(); ExpectStop(1U,1U);
    CHECK(real_core.stop_pending==1U && real_core.stop_applied_ticket==0U);
    CHECK(AxisCommand_NeedsDisarm(&real_core,3U,0U)!=0U);
    QueueStop(1U); CanLink_Poll(); ExpectStop(1U,1U);
    CHECK(stop_calls==1U);
    AxisCommand_Tick(&real_core,3U,mode==3U?0x80U:0U,1U);
    CHECK(real_core.stop_pending==0U && real_core.stop_applied_ticket==1U);
    CHECK(real_core.active_valid==0U && real_core.pending_ready==0U);
    CHECK(real_core.result==AXIS_CMD_STOPPED && real_core.run_epoch>epoch);
    CHECK(AxisCommand_Permitted(&real_core,3U,mode==3U?0x80U:0U)==0U);
    if(pending_seq) CHECK(real_core.cancelled_seq==pending_seq);
    if(mode==3U) CHECK(real_core.fault_latched!=0U);
    QueueStop(1U); CanLink_Poll(); ExpectStop(1U,2U);
    epoch=real_core.run_epoch;
    QueueStop(1U); CanLink_Poll(); ExpectStop(1U,2U);
    CHECK(stop_calls==1U && real_core.run_epoch==epoch);
    if(mode==3U) CHECK(real_core.fault_latched!=0U);
    if(mode==2U) CHECK(real_core.config_valid==0U);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    Setup();
    if (!strcmp(argv[1], "valid")) Valid();
    else if (!strcmp(argv[1], "lengths")) Lengths();
    else if (!strcmp(argv[1], "rejects")) Rejects();
    else if (!strcmp(argv[1], "busy")) Busy();
    else if (!strcmp(argv[1], "rx-fail")) HalFailure(0);
    else if (!strcmp(argv[1], "tx-fail")) HalFailure(1);
    else if (!strcmp(argv[1], "errors")) Errors();
    else if (!strcmp(argv[1], "busoff-ir")) Latched(1, 0);
    else if (!strcmp(argv[1], "busoff-psr")) Latched(1, 1);
    else if (!strcmp(argv[1], "ram-error")) Latched(0, 0);
    else if (!strcmp(argv[1], "bad-index")) RuntimeGeometry(0);
    else if (!strcmp(argv[1], "overwrite")) RuntimeGeometry(1);
    else if (!strcmp(argv[1], "query-valid")) QueryValid();
    else if (!strcmp(argv[1], "query-status")) QueryStatus();
    else if (!strcmp(argv[1], "query-rejects")) QueryRejects();
    else if (!strcmp(argv[1], "query-dlc")) QueryDlc();
    else if (!strcmp(argv[1], "query-busy")) QueryBusy();
    else if (!strcmp(argv[1], "query-rx-fail")) QueryHalFailure(0);
    else if (!strcmp(argv[1], "query-tx-fail")) QueryHalFailure(1);
    else if (!strcmp(argv[1], "query-busoff")) QueryBusOff();
    else if (!strcmp(argv[1], "no-provider")) NoProvider();
    else if (!strcmp(argv[1], "stop-valid")) StopValid();
    else if (!strcmp(argv[1], "stop-ordering")) StopOrdering();
    else if (!strcmp(argv[1], "stop-tx-busy")) StopRetry(0);
    else if (!strcmp(argv[1], "stop-tx-fail")) StopRetry(1);
    else if (!strcmp(argv[1], "stop-rejected")) StopRejected();
    else if (!strcmp(argv[1], "stop-invalid")) StopInvalid();
    else if (!strcmp(argv[1], "stop-dlc")) StopDlc();
    else if (!strcmp(argv[1], "stop-busoff")) StopUnavailable(1);
    else if (!strcmp(argv[1], "stop-rx-fail")) StopUnavailable(0);
    else if (!strcmp(argv[1], "stop-no-provider")) NoStopProvider();
    else if (!strcmp(argv[1], "stop-max-id")) StopMaxId();
    else if (!strncmp(argv[1], "stop-core-",10)) StopCore((unsigned)atoi(argv[1]+10));
    else if (!strncmp(argv[1], "bad-config-", 11)) BadConfig((unsigned)atoi(argv[1] + 11));
    else if (!strncmp(argv[1], "init-fail-", 10)) InitFailure((unsigned)atoi(argv[1] + 10));
    else CHECK(0);
    if(strncmp(argv[1],"stop-",5)) CHECK(stop_calls==0U);
    CHECK(ignored_stop_calls==0U);
    printf("CAN service %s: %u assertions PASS\n", argv[1], checks);
    return 0;
}
