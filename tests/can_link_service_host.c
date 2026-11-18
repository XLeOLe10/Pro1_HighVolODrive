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
HostDebugRegisters host_debug_registers;
HostRccRegisters host_rcc_registers;
HostCanCcuRegisters host_ccu_registers;
uint32_t host_message_ram[161];
FDCAN_HandleTypeDef hfdcan1;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned config_calls, global_calls, blocking_calls, start_calls;
static unsigned receive_calls, send_calls, fill_calls, free_calls, clear_calls;
static unsigned fail_init_at;
static unsigned time_init_calls, time_init_seen, timeout_resets, timestamp_resets;
static unsigned fail_time_at, bad_time_at, fail_runtime_reset;
static uint32_t kernel_clock_hz = 25000000U;
enum { TIME_HOOK_CLOCK=1U, TIME_HOOK_RESET_BEFORE, TIME_HOOK_RX,
       TIME_HOOK_FILL, TIME_HOOK_START, TIME_HOOK_RESET_AFTER };
static unsigned time_hook_site, time_hook_skip, time_hook_action, time_hook_fired;
static void TimeHook(unsigned site)
{
    if(time_hook_site!=site)return;
    if(time_hook_skip!=0U) { --time_hook_skip;return; }
    time_hook_site=0U;++time_hook_fired;
    if(time_hook_action==1U)host_can_registers.IR|=FDCAN_IR_TOO;
    if(time_hook_action==2U)host_can_registers.TOCV=0U;
    if(time_hook_action==3U)host_can_registers.NBTP^=1U<<FDCAN_NBTP_NBRP_Pos;
    if(time_hook_action==4U)host_can_registers.IR|=FDCAN_IR_RF0L;
    if(time_hook_action==5U)host_can_registers.IR|=FDCAN_IR_MRAF;
    if(time_hook_action==6U)host_can_registers.IR|=FDCAN_IR_WDI;
    if(time_hook_action==7U)host_can_registers.IR|=FDCAN_IR_BO;
}
static uint32_t fifo_fill, fifo_free = 32U, cleared_flags;
static HAL_StatusTypeDef receive_result = HAL_OK, send_result = HAL_OK;
static FDCAN_RxHeaderTypeDef incoming;
static uint8_t incoming_data[8];
static FDCAN_TxHeaderTypeDef sent_header;
static uint8_t sent_data[8];
static unsigned status_reads, ignored_status_reads;
static unsigned boot_reads, ignored_boot_reads;
static uint32_t boot_state=1U;
static uint32_t boot_words[4]={0x01234567U,0x89ABCDEFU,0U,0xFFFFFFFFU};
static uint32_t ReadBoot(uint32_t words[4])
{
    CHECK(words!=NULL);
    ++boot_reads;
    /* Deliberately fill even when invalid: service must suppress these words. */
    memcpy(words,boot_words,sizeof(boot_words));
    return boot_state;
}
static uint32_t IgnoredBoot(uint32_t words[4])
{
    (void)words;++ignored_boot_reads;return 2U;
}
static CanLinkReadStatus observed_status;
static unsigned stop_calls, ignored_stop_calls;
static uint32_t stop_success=1U, stop_ticket=41U;
static unsigned use_core;
static AxisCommandCore real_core;
static unsigned target_calls,ignored_target_calls,target_use_limits;
static int32_t checked_speed;
static uint32_t checked_iq;
static uint8_t target_result=1U;
static AxisCommandLimits target_limits={1U,1000.0f,4.0f,1000U};
static uint8_t CheckTargets(int32_t rpm,uint32_t iq_ma)
{
    uint32_t result;
    ++target_calls; checked_speed=rpm; checked_iq=iq_ma;
    if(!target_use_limits)return target_result;
    result=AxisCommand_ValidateTargets(&target_limits,(float)rpm,(float)iq_ma/1000.0f);
    if(result==AXIS_CMD_OK)return 1U;
    if(result==AXIS_CMD_CONFIG_UNSET)return 3U;
    if(result==AXIS_CMD_INVALID)return 2U;
    return 4U;
}
static uint8_t IgnoredTargets(int32_t rpm,uint32_t iq_ma)
{
    (void)rpm;(void)iq_ma;++ignored_target_calls;return 4U;
}
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
    CHECK((mask & FDCAN_IR_TOO)==0U); /* Never clear the elapsed-gap evidence. */
    cleared_flags = mask;
    /* Emulate W1C, preserving any unrelated pending flags. */
    h->Instance->IR &= ~mask;
}
static HAL_StatusTypeDef TimeInitStep(FDCAN_HandleTypeDef *h,unsigned step)
{
    CHECK(h==&hfdcan1 && h->State==HAL_FDCAN_STATE_READY && start_calls==0U);
    ++time_init_calls;time_init_seen|=1U<<step;
    return fail_time_at==step ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigTimestampCounter(FDCAN_HandleTypeDef *h,uint32_t prescaler)
{
    HAL_StatusTypeDef result=TimeInitStep(h,1U);
    CHECK(prescaler==0x000F0000U);
    if(result==HAL_OK && bad_time_at!=1U)h->Instance->TSCC=(h->Instance->TSCC&~FDCAN_TSCC_TCP)|prescaler;
    return result;
}
HAL_StatusTypeDef HAL_FDCAN_EnableTimestampCounter(FDCAN_HandleTypeDef *h,uint32_t operation)
{
    HAL_StatusTypeDef result=TimeInitStep(h,2U);
    CHECK(operation==1U);
    if(result==HAL_OK && bad_time_at!=2U)h->Instance->TSCC=(h->Instance->TSCC&~FDCAN_TSCC_TSS)|operation;
    return result;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigTimeoutCounter(FDCAN_HandleTypeDef *h,uint32_t operation,uint32_t period)
{
    HAL_StatusTypeDef result=TimeInitStep(h,3U);
    CHECK(operation==0U && period==6250U);
    if(result==HAL_OK && bad_time_at!=3U)
        h->Instance->TOCC=(h->Instance->TOCC&~(FDCAN_TOCC_TOS|FDCAN_TOCC_TOP))|
                         operation|(period<<FDCAN_TOCC_TOP_Pos);
    return result;
}
HAL_StatusTypeDef HAL_FDCAN_EnableTimeoutCounter(FDCAN_HandleTypeDef *h)
{
    HAL_StatusTypeDef result=TimeInitStep(h,4U);
    if(result==HAL_OK && bad_time_at!=4U)h->Instance->TOCC|=FDCAN_TOCC_ETOC;
    return result;
}
HAL_StatusTypeDef HAL_FDCAN_ResetTimestampCounter(FDCAN_HandleTypeDef *h)
{
    HAL_StatusTypeDef result=TimeInitStep(h,5U);
    ++timestamp_resets;
    if(result==HAL_OK)h->Instance->TSCV=0U;
    return result;
}
HAL_StatusTypeDef HAL_FDCAN_ResetTimeoutCounter(FDCAN_HandleTypeDef *h)
{
    HAL_StatusTypeDef result=HAL_OK;
    CHECK(h==&hfdcan1);++timeout_resets;
    if(start_calls==0U)result=TimeInitStep(h,6U);
    else if(fail_runtime_reset)result=HAL_ERROR;
    TimeHook(TIME_HOOK_RESET_BEFORE);
    if(result==HAL_OK)h->Instance->TOCV=(h->Instance->TOCC&FDCAN_TOCC_TOP)>>FDCAN_TOCC_TOP_Pos;
    TimeHook(TIME_HOOK_RESET_AFTER);
    return result;
}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t peripheral)
{
    CHECK(peripheral==RCC_PERIPHCLK_FDCAN);
    TimeHook(TIME_HOOK_CLOCK);
    return kernel_clock_hz;
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
    h->Instance->CCCR&=~(FDCAN_CCCR_INIT|FDCAN_CCCR_CCE);
    TimeHook(TIME_HOOK_START);
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *h, uint32_t fifo)
{
    CHECK(h == &hfdcan1 && fifo == FDCAN_RX_FIFO0);
    ++fill_calls;
    TimeHook(TIME_HOOK_FILL);
    return fifo_fill;
}
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t fifo, FDCAN_RxHeaderTypeDef *out, uint8_t *data)
{
    uint32_t index = (h->Instance->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    CHECK(h == &hfdcan1 && fifo == FDCAN_RX_FIFO0 && fifo_fill != 0U);
    ++receive_calls;
    TimeHook(TIME_HOOK_RX);
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
    hfdcan1.Init.NominalPrescaler=5U;hfdcan1.Init.NominalSyncJumpWidth=1U;
    hfdcan1.Init.NominalTimeSeg1=7U;hfdcan1.Init.NominalTimeSeg2=2U;
    host_can_registers.NBTP=0x00040601U;
    host_can_registers.CCCR=FDCAN_CCCR_INIT|FDCAN_CCCR_CCE|FDCAN_CCCR_DAR;
    host_can_registers.RXF0C=(8U<<FDCAN_RXF0C_F0S_Pos)|4U;
    host_ccu_registers.CCFG=FDCANCCU_CCFG_BCC;
    host_debug_registers.APB1HFZ1=DBGMCU_APB1HFZ1_DBG_FDCAN;
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
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state == 1U && can_link_error_count == 0U);
    CHECK(config_calls == 1U && global_calls == 1U && blocking_calls == 1U && start_calls == 1U);
    CanLink_Init(IgnoredStatus,IgnoredStop,IgnoredTargets,IgnoredBoot);
    CHECK(config_calls == 1U && start_calls == 1U && boot_reads == 0U);
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_time_diag.reason_flags==0U);
    CHECK(can_link_time_diag.last_rx_age_us==UINT32_MAX);
    CHECK(time_init_calls==6U && time_init_seen==0x7EU && timestamp_resets==1U && timeout_resets==1U);
    CHECK((host_debug_registers.APB1HFZ1&DBGMCU_APB1HFZ1_DBG_FDCAN)==0U);
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
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot); CanLink_Poll();
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
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state == 2U && can_link_diag.error_flags == CAN_LINK_ERROR_CONFIG);
    CHECK(can_link_error_count == 1U && config_calls == 0U && start_calls == 0U);
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot); CanLink_Poll();
    CHECK(can_link_error_count == 1U && receive_calls == 0U && start_calls == 0U);
}
static void InitFailure(unsigned step)
{
    fail_init_at = step;
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state == 2U && can_link_diag.error_flags == CAN_LINK_ERROR_INIT);
    CHECK(can_link_error_count == 1U);
    CHECK(config_calls == 1U && global_calls == (step >= 2U));
    CHECK(blocking_calls == (step >= 3U) && start_calls == (step >= 4U));
    fail_init_at = 0U;
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot); CanLink_Poll();
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
        {0x52U,0x01U,0x01U,0xA5U,0xFFU,0x01U,0x01U,0U},
        {0x52U,0x01U,0x02U,0xA6U,0x05U,0x19U,0xCDU,0xABU},
        {0x52U,0x01U,0x03U,0xA7U,0xEFU,0xBEU,0xADU,0xDEU},
        {0x52U,0x01U,0x04U,0xA8U,0x06U,0x08U,0U,0U}
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
        case 4: incoming_data[2]=12U; break; /* Unknown query */
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
    CanLink_Init(NULL,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state==2U);
    CHECK(can_link_error_count==1U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CHECK(config_calls==0U && start_calls==0U);
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
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
    CanLink_Init(ReadStatus,NULL,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state==2U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
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


static void QueueTarget(uint8_t token,uint16_t speed_bits,uint16_t iq_ma)
{
    Queue(8U,0U,1U);
    incoming_data[0]=0x43U;incoming_data[1]=1U;incoming_data[2]=0x20U;incoming_data[3]=token;
    incoming_data[4]=(uint8_t)speed_bits;incoming_data[5]=(uint8_t)(speed_bits>>8U);
    incoming_data[6]=(uint8_t)iq_ma;incoming_data[7]=(uint8_t)(iq_ma>>8U);
}
static void ExpectTarget(uint8_t result)
{
    uint8_t expected[8];
    memcpy(expected,incoming_data,sizeof(expected));
    expected[0]=0x56U;expected[2]=result;
    CHECK(memcmp(expected,sent_data,sizeof(expected))==0);
}
static void TargetValid(void)
{
    unsigned result;
    Ready();
    for(result=1U;result<=4U;++result) {
        target_result=(uint8_t)result;
        QueueTarget((uint8_t)result,0xFF9CU,500U);fifo_fill=8U;
        CanLink_Poll();ExpectTarget((uint8_t)result);
        CHECK(checked_speed==-100 && checked_iq==500U);
        CHECK(target_calls==result && receive_calls==result && send_calls==result);
        CHECK(fifo_fill==7U); /* <=1 request per poll, independent of result */
        CHECK(can_link_rx_count==0U && can_query_rx_count==0U && stop_calls==0U);
    }
    QueueTarget(4U,0xFF9CU,500U);CanLink_Poll();ExpectTarget(4U);
    CHECK(target_calls==5U); /* Duplicate is rechecked, never retained as a command. */
    fifo_fill=0U;CanLink_Poll();
    CHECK(target_calls==5U && send_calls==5U && status_reads==0U);
    CHECK(can_link_error_count==0U && can_link_diag.tx_enqueued_count==5U);
}
static void TargetRejects(void)
{
    unsigned i;
    Ready();
    for(i=0U;i<10U;++i) {
        QueueTarget(0U,100U,500U);
        switch(i) {
        case 0:incoming_data[0]=0x56U;break;
        case 1:incoming_data[1]=2U;break;
        case 2:incoming_data[2]=0x21U;break;
        case 3:incoming.Identifier=0x322U;break;
        case 4:incoming.IdType=1U;break;
        case 5:incoming.RxFrameType=1U;break;
        case 6:incoming.FDFormat=1U;break;
        case 7:incoming.BitRateSwitch=1U;break;
        case 8:incoming.DataLength=7U;break;
        default:incoming_data[0]=0x51U;break;
        }
        CanLink_Poll();
        CHECK(target_calls==0U && stop_calls==0U && status_reads==0U && send_calls==0U);
        CHECK(can_link_diag.rejected_count==i+1U);
    }
}
static void TargetDlc(void)
{
    unsigned dlc;
    Ready();
    for(dlc=0U;dlc<16U;++dlc) {
        unsigned rx=receive_calls,tx=send_calls,check=target_calls;
        QueueTarget(0U,100U,500U);host_message_ram[2U]=dlc<<16U;
        CanLink_Poll();
        if(dlc==8U) {
            CHECK(receive_calls==rx+1U && send_calls==tx+1U && target_calls==check+1U);
        } else {
            CHECK(receive_calls==rx && send_calls==tx && target_calls==check);
            CHECK(host_can_registers.RXF0A==0U);
        }
    }
    CHECK(can_link_diag.rejected_count==15U && target_calls==1U);
}
static void TargetFailure(unsigned mode)
{
    Ready();QueueTarget(0xA5U,0x8000U,0xFFFFU);
    if(mode==0U)fifo_free=0U;
    else if(mode==1U)send_result=HAL_ERROR;
    else if(mode==2U)receive_result=HAL_ERROR;
    else host_can_registers.PSR=FDCAN_PSR_BO;
    CanLink_Poll();
    CHECK(target_calls==(mode<2U?1U:0U));
    CHECK(send_calls==(mode==1U?1U:0U));
    CHECK(receive_calls==(mode==3U?0U:1U));
    CHECK(can_link_error_count==1U && can_link_diag.tx_enqueued_count==0U);
    CHECK(can_link_diag.error_flags==(mode==0U?CAN_LINK_ERROR_TX_BUSY:
        mode==3U?CAN_LINK_ERROR_BUS_OFF:CAN_LINK_ERROR_HAL));
    if(mode<2U) {
        unsigned prev=send_calls;
        fifo_free=32U;send_result=HAL_OK;fifo_fill=0U;CanLink_Poll();
        CHECK(send_calls==prev && target_calls==1U); /* No delayed command/reply. */
        QueueTarget(0xA5U,0x8000U,0xFFFFU);CanLink_Poll();ExpectTarget(1U);
        CHECK(target_calls==2U && checked_speed==-32768 && checked_iq==65535U);
    }
}
static void TargetMissing(void)
{
    CanLink_Init(ReadStatus,RequestStop,NULL,ReadBoot);
    CHECK(can_link_diag.state==2U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    QueueTarget(0U,0U,0U);CanLink_Poll();
    CHECK(config_calls==0U && target_calls==0U && send_calls==0U);
    CHECK(can_link_error_count==1U);
}
static void TargetBadProvider(void)
{
    Ready();
    target_result=0U;QueueTarget(0U,0U,0U);CanLink_Poll();ExpectTarget(4U);
    CHECK(can_link_error_count==1U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    target_result=255U;QueueTarget(0U,0U,0U);CanLink_Poll();ExpectTarget(4U);
    CHECK(can_link_error_count==2U && can_link_diag.state==1U);
}
static void TargetCore(void)
{
    static const uint16_t speeds[]={0U,1000U,0xFC18U,1001U,0xFC17U,0x8000U,0U,0x7FFFU};
    static const uint16_t currents[]={0U,4000U,4000U,0U,500U,65535U,4001U,4000U};
    static const uint8_t results[]={1U,1U,1U,2U,2U,2U,2U,2U};
    AxisCommandCore before;
    unsigned i;
    uint32_t active_seq,pending_seq;
    AxisCommand_Init(&real_core,&target_limits);use_core=1U;target_use_limits=1U;
    active_seq=PublishFixture(AXIS_CMD_START,1U);
    AxisCommand_Tick(&real_core,1U,0U,1U);
    pending_seq=PublishFixture(AXIS_CMD_UPDATE,2U);
    CHECK(real_core.active_valid && real_core.pending_ready && active_seq!=pending_seq);
    before=real_core;
    Ready();
    for(i=0U;i<sizeof(results)/sizeof(results[0]);++i) {
        QueueTarget((uint8_t)i,speeds[i],currents[i]);CanLink_Poll();ExpectTarget(results[i]);
        CHECK(memcmp(&before,&real_core,sizeof(before))==0);
    }
    target_limits.confirmed=0U;
    QueueTarget(0U,100U,500U);CanLink_Poll();ExpectTarget(3U);
    CHECK(memcmp(&before,&real_core,sizeof(before))==0);
    target_limits.confirmed=1U;
    /* Fault and stale state do not turn numeric preflight into authorization. */
    AxisCommand_Tick(&real_core,3U,0x80U,1U);before=real_core;
    QueueTarget(0U,100U,500U);CanLink_Poll();ExpectTarget(1U);
    CHECK(real_core.fault_latched && memcmp(&before,&real_core,sizeof(before))==0);
    CHECK(stop_calls==0U && status_reads==0U);
}


static void StopTargetRetained(void)
{
    Ready();
    QueueStop(7U);CanLink_Poll();ExpectStop(7U,1U);
    QueueTarget(9U,100U,500U);CanLink_Poll();ExpectTarget(1U);
    QueueStop(7U);CanLink_Poll();ExpectStop(7U,1U);
    CHECK(stop_calls==1U && target_calls==1U);
    observed_status.stop_applied_ticket=41U;
    target_result=2U;QueueTarget(10U,1001U,4001U);CanLink_Poll();ExpectTarget(2U);
    QueueStop(7U);CanLink_Poll();ExpectStop(7U,2U);
    CHECK(stop_calls==1U && target_calls==2U && can_link_error_count==0U);
}

/* New identity queries use the existing bounded read-only service. A valid
 * identity may contain individual zero words; only the provider owns validity. */
static void ExpectBoot(uint8_t opcode,uint8_t token,uint32_t state)
{
    static const uint8_t wire_words[4][4]={
        {0x67U,0x45U,0x23U,0x01U}, {0xEFU,0xCDU,0xABU,0x89U},
        {0U,0U,0U,0U}, {0xFFU,0xFFU,0xFFU,0xFFU}
    };
    uint8_t expected[8]={0x52U,0x01U,0U,0U,0U,0U,0U,0U};
    expected[2]=opcode;expected[3]=token;
    if(opcode==5U)expected[4]=(uint8_t)state;
    else if(state==1U)memcpy(expected+4U,wire_words[opcode-6U],4U);
    CHECK(memcmp(sent_data,expected,8U)==0);
}
static void BootQueries(uint32_t state)
{
    AxisCommandCore before;
    CanLinkReadStatus status_before;
    uint32_t words_before[4];
    unsigned repeat,opcode,t,count=0U;
    static const uint8_t tokens[]={0U,255U};
    AxisCommand_Init(&real_core,&target_limits);use_core=1U;
    (void)PublishFixture(AXIS_CMD_START,1U);
    AxisCommand_Tick(&real_core,1U,0U,1U);
    (void)PublishFixture(AXIS_CMD_UPDATE,2U);
    CHECK(real_core.active_valid && real_core.pending_ready);
    before=real_core;status_before=observed_status;
    boot_state=state;memcpy(words_before,boot_words,sizeof(words_before));
    Ready();
    for(repeat=0U;repeat<2U;++repeat) {
        for(opcode=5U;opcode<=9U;++opcode) {
            for(t=0U;t<2U;++t) {
                QueueQuery((uint8_t)opcode,tokens[t]);fifo_fill=8U;
                CanLink_Poll();++count;
                ExpectBoot((uint8_t)opcode,tokens[t],state);
                CHECK(boot_reads==count && send_calls==count && receive_calls==count);
                CHECK(can_query_rx_count==count && can_link_rx_count==0U && fifo_fill==7U);
                CHECK(memcmp(&before,&real_core,sizeof(before))==0);
                CHECK(memcmp(&status_before,&observed_status,sizeof(status_before))==0);
                CHECK(boot_state==state && memcmp(words_before,boot_words,sizeof(words_before))==0);
                CHECK(status_reads==0U && stop_calls==0U && target_calls==0U);
            }
        }
    }
    fifo_fill=0U;CanLink_Poll();
    CHECK(boot_reads==count && send_calls==count && can_link_error_count==0U);
    CHECK(can_link_diag.tx_enqueued_count==count && can_link_diag.rejected_count==0U);
}
static void BootRejects(void)
{
    unsigned opcode,i,count=0U;
    Ready();
    for(opcode=5U;opcode<=9U;++opcode) {
        for(i=0U;i<16U;++i) {
            QueueQuery((uint8_t)opcode,0xA5U);
            switch(i) {
            case 0:incoming.Identifier=0x322U;break;
            case 1:incoming.IdType=2U;break;
            case 2:incoming.RxFrameType=255U;break;
            case 3:incoming.FDFormat=1U;break;
            case 4:incoming.BitRateSwitch=1U;break;
            case 5:incoming.DataLength=7U;break;
            case 6:incoming_data[0]=0x52U;break;
            case 7:incoming_data[1]=0U;break;
            case 8:incoming_data[1]=2U;break;
            case 9:incoming_data[2]=0U;break;
            case 10:incoming_data[2]=12U;break;
            case 11:incoming_data[2]=255U;break;
            default:incoming_data[i-8U]=0x80U;break;
            }
            CanLink_Poll();++count;
            CHECK(can_link_diag.rejected_count==count);
            CHECK(boot_reads==0U && status_reads==0U && target_calls==0U && stop_calls==0U);
            CHECK(send_calls==0U && free_calls==0U && can_query_rx_count==0U);
        }
    }
    CHECK(can_link_error_count==0U && can_link_rx_count==0U);
}
static void BootDlc(void)
{
    unsigned opcode,dlc;
    Ready();
    for(opcode=5U;opcode<=9U;++opcode) {
        for(dlc=0U;dlc<16U;++dlc) {
            unsigned r=receive_calls,t=send_calls,b=boot_reads;
            QueueQuery((uint8_t)opcode,0U);host_message_ram[2U]=dlc<<16U;
            CanLink_Poll();
            if(dlc==8U) {
                CHECK(receive_calls==r+1U && send_calls==t+1U && boot_reads==b+1U);
                ExpectBoot((uint8_t)opcode,0U,1U);
            } else {
                CHECK(receive_calls==r && send_calls==t && boot_reads==b);
                CHECK(host_can_registers.RXF0A==0U);
            }
        }
    }
    CHECK(can_link_diag.rejected_count==75U && can_query_rx_count==5U);
    CHECK(can_link_rx_count==0U && can_link_error_count==0U);
}
static void BootFailure(unsigned mode)
{
    uint32_t words_before[4];
    Ready();memcpy(words_before,boot_words,sizeof(words_before));
    QueueQuery(6U,255U);
    if(mode==0U)fifo_free=0U;
    if(mode==1U)send_result=HAL_ERROR;
    if(mode==2U)receive_result=HAL_ERROR;
    if(mode==3U)host_can_registers.PSR=FDCAN_PSR_BO;
    CanLink_Poll();
    CHECK(boot_reads==(mode<2U?1U:0U));
    CHECK(can_query_rx_count==(mode<2U?1U:0U));
    CHECK(receive_calls==(mode==3U?0U:1U));
    CHECK(send_calls==(mode==1U?1U:0U));
    CHECK(can_link_diag.tx_enqueued_count==0U && can_link_error_count==1U);
    CHECK(can_link_diag.error_flags==(mode==0U?CAN_LINK_ERROR_TX_BUSY:
          (mode==3U?CAN_LINK_ERROR_BUS_OFF:CAN_LINK_ERROR_HAL)));
    CHECK(boot_state==1U && memcmp(words_before,boot_words,sizeof(words_before))==0);
    CHECK(status_reads==0U && target_calls==0U && stop_calls==0U);
    if(mode<3U) {
        unsigned b=boot_reads,t=send_calls;
        fifo_fill=0U;fifo_free=32U;send_result=HAL_OK;receive_result=HAL_OK;
        CanLink_Poll();CHECK(boot_reads==b && send_calls==t);
        QueueQuery(6U,255U);CanLink_Poll();ExpectBoot(6U,255U,1U);
        CHECK(boot_reads==b+1U && send_calls==t+1U && can_link_diag.tx_enqueued_count==1U);
    } else {
        host_can_registers.PSR=0U;CanLink_Poll();
        CHECK(can_link_diag.state==3U && boot_reads==0U && receive_calls==0U);
    }
}
static void BootMissing(void)
{
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,NULL);
    CHECK(can_link_diag.state==2U && can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG);
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    QueueQuery(5U,0U);CanLink_Poll();
    CHECK(config_calls==0U && start_calls==0U && receive_calls==0U && send_calls==0U);
    CHECK(boot_reads==0U && status_reads==0U && can_link_error_count==1U);
}
static void BootBadProvider(void)
{
    unsigned i,opcode,count=0U;
    static const uint32_t invalid[]={3U,0xFFFFFFFFU};
    Ready();
    for(i=0U;i<2U;++i) {
        boot_state=invalid[i];
        for(opcode=5U;opcode<=9U;++opcode) {
            QueueQuery((uint8_t)opcode,0U);CanLink_Poll();++count;
            ExpectBoot((uint8_t)opcode,0U,2U);
            CHECK(boot_reads==count && can_link_error_count==count);
            CHECK(can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG && can_link_diag.state==1U);
        }
    }
}
static void StopBootRetained(void)
{
    unsigned opcode;
    Ready();QueueStop(7U);CanLink_Poll();ExpectStop(7U,1U);
    for(opcode=5U;opcode<=9U;++opcode) {
        QueueQuery((uint8_t)opcode,7U);CanLink_Poll();ExpectBoot((uint8_t)opcode,7U,1U);
    }
    QueueStop(7U);CanLink_Poll();ExpectStop(7U,1U);
    CHECK(stop_calls==1U && boot_reads==5U && target_calls==0U);
    observed_status.stop_applied_ticket=41U;boot_state=2U;
    QueueQuery(6U,7U);CanLink_Poll();ExpectBoot(6U,7U,2U);
    QueueStop(7U);CanLink_Poll();ExpectStop(7U,2U);
    CHECK(stop_calls==1U && boot_reads==6U && can_link_error_count==0U);
}

/* Hardware time tests use actual service/register contracts. Every mutation
 * below runs in a new process; restoring mock registers never resets private
 * sticky state. Mocks prove software ordering, not oscillator accuracy. */
static void SetFrameClock(uint32_t now,uint32_t received)
{
    uint32_t index=(host_can_registers.RXF0S&FDCAN_RXF0S_F0GI)>>FDCAN_RXF0S_F0GI_Pos;
    CHECK(index<8U);
    host_can_registers.TSCV=now&0xFFFFU;
    incoming.RxTimestamp=received&0xFFFFU;
    host_message_ram[2U+index*4U]=(host_message_ram[2U+index*4U]&0xFFFF0000U)|(received&0xFFFFU);
}
static void ExpectTime(uint8_t op,uint8_t token,uint32_t value)
{
    CHECK(sent_header.Identifier==0x322U && sent_header.DataLength==FDCAN_DLC_BYTES_8);
    CHECK(sent_data[0]==0x52U && sent_data[1]==1U && sent_data[2]==op && sent_data[3]==token);
    CHECK(sent_data[4]==(uint8_t)value && sent_data[5]==(uint8_t)(value>>8U) &&
          sent_data[6]==(uint8_t)(value>>16U) && sent_data[7]==(uint8_t)(value>>24U));
}
static void CheckTimeLost(uint32_t reasons)
{
    CHECK(can_link_time_diag.state==CAN_TIME_LOST);
    CHECK(can_link_time_diag.reason_flags==reasons);
    CHECK(can_link_time_diag.last_rx_age_us==UINT32_MAX);
}
static void TimeNormal(void)
{
    unsigned resets;
    Ready();
    CHECK(CAN_TIME_TICK_US==32U && CAN_TIME_SERVICE_TICKS==6250U);
    QueueQuery(10U,0U);SetFrameClock(1U,0U);CanLink_Poll();ExpectTime(10U,0U,1U);
    CHECK(can_link_time_diag.last_rx_age_us==32U);
    QueueQuery(11U,255U);SetFrameClock(0x10U,0xFFF0U);
    host_can_registers.IR|=FDCAN_IR_TSW;
    CanLink_Poll();ExpectTime(11U,255U,1024U);
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_time_diag.reason_flags==0U);
    CHECK(can_link_time_diag.last_rx_age_us==1024U);
    resets=timeout_resets;fifo_fill=0U;host_can_registers.TSCV=0xFFFFU;
    CanLink_Poll();CHECK(timeout_resets==resets+1U);
    CHECK(can_link_time_diag.last_rx_age_us==1024U && receive_calls==2U);
    QueueQuery(11U,1U);SetFrameClock(700U,700U);CanLink_Poll();ExpectTime(11U,1U,0U);
    CHECK(can_link_error_count==0U && status_reads==0U && can_query_rx_count==3U);
}
static void TimeMetadata(void)
{
    unsigned i;
    Ready();
    for(i=0U;i<7U;++i) {
        QueueQuery(11U,(uint8_t)i);SetFrameClock(100U+i,80U);
        switch(i) {
        case 0U:incoming.Identifier=0x320U;break;
        case 1U:incoming.IdType=1U;break;
        case 2U:incoming.RxFrameType=1U;break;
        case 3U:incoming.FDFormat=1U;break;
        case 4U:incoming.BitRateSwitch=1U;break;
        case 5U:incoming_data[0]=0U;break;
        default:incoming_data[4]=1U;break;
        }
        CanLink_Poll();CHECK(receive_calls==i+1U && send_calls==0U);
        CHECK(can_link_diag.rejected_count==i+1U);
        CHECK(can_link_time_diag.state==CAN_TIME_TRACKING);
        CHECK(can_link_time_diag.last_rx_age_us==(20U+i)*32U);
    }
    Queue(15U,6U,1U);CanLink_Poll();
    CHECK(host_can_registers.RXF0A==6U && receive_calls==7U);
    CHECK(can_link_time_diag.last_rx_age_us==UINT32_MAX);
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_error_count==0U);
    fifo_fill=0U;CanLink_Poll();CHECK(can_link_time_diag.last_rx_age_us==UINT32_MAX);
}
static void TimeBoundedProgress(void)
{
    unsigned i,before;
    Ready();
    for(i=0U;i<8U;++i) {
        before=receive_calls;
        Queue(i%2U?8U:15U,i,8U-i);
        SetFrameClock(60000U,5000U+i*6250U);
        CanLink_Poll();
        CHECK(host_can_registers.RXF0A==i);
        CHECK(receive_calls==before+(i%2U));
        CHECK(can_link_time_diag.state==CAN_TIME_TRACKING);
        CHECK(timeout_resets==2U+i);
        if(i%2U) {
            CHECK(fifo_fill==7U-i);
            CHECK(can_link_time_diag.last_rx_age_us==(55000U-i*6250U)*32U);
        } else CHECK(can_link_time_diag.last_rx_age_us==UINT32_MAX);
    }
    CHECK(receive_calls==4U && send_calls==4U && can_link_diag.rejected_count==4U);
    CHECK(can_link_error_count==0U);
}
static void TimeSticky(unsigned zero_counter)
{
    unsigned resets;
    Ready();QueueQuery(11U,1U);SetFrameClock(2U,1U);CanLink_Poll();
    CHECK(can_link_time_diag.last_rx_age_us==32U);
    if(zero_counter)host_can_registers.TOCV=0U;
    else host_can_registers.IR|=FDCAN_IR_TOO|FDCAN_IR_TSW|FDCAN_IR_RF0N|FDCAN_IR_TC;
    resets=timeout_resets;
    QueueQuery(10U,2U);CanLink_Poll();ExpectTime(10U,2U,2U);
    CheckTimeLost(CAN_TIME_LOSS_TIMEOUT);CHECK(timeout_resets==resets);
    CHECK((cleared_flags&FDCAN_IR_TOO)==0U);
    if(!zero_counter)CHECK((host_can_registers.IR&FDCAN_IR_TOO)!=0U);
    host_can_registers.IR=0U;host_can_registers.TOCV=6250U;
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    QueueQuery(11U,3U);SetFrameClock(3U,3U);CanLink_Poll();ExpectTime(11U,3U,UINT32_MAX);
    CheckTimeLost(CAN_TIME_LOSS_TIMEOUT);
    CHECK(timeout_resets==resets && timestamp_resets==1U && start_calls==1U);
    CHECK(can_link_diag.state==1U && can_link_error_count==0U);
}
static void TimeWatch(void)
{
    unsigned resets;
    Ready();
    can_link_time_diag.state=CAN_TIME_LOST;can_link_time_diag.reason_flags=0xFFFFFFFFU;
    can_link_time_diag.last_rx_age_us=0U;
    QueueQuery(10U,1U);SetFrameClock(12U,10U);CanLink_Poll();ExpectTime(10U,1U,1U);
    CHECK(can_link_time_diag.reason_flags==0U && can_link_time_diag.last_rx_age_us==64U);
    host_can_registers.IR=FDCAN_IR_TOO;QueueQuery(10U,2U);CanLink_Poll();
    ExpectTime(10U,2U,2U);resets=timeout_resets;
    host_can_registers.IR=0U;host_can_registers.TOCV=6250U;
    can_link_time_diag.state=CAN_TIME_TRACKING;can_link_time_diag.reason_flags=0U;
    can_link_time_diag.last_rx_age_us=0U;
    QueueQuery(11U,3U);CanLink_Poll();ExpectTime(11U,3U,UINT32_MAX);
    CheckTimeLost(CAN_TIME_LOSS_TIMEOUT);CHECK(timeout_resets==resets);
    CHECK(can_link_error_count==0U);
}
static void TimeInitFailure(unsigned step)
{
    unsigned calls,resets;
    fail_time_at=step;CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state==1U && start_calls==1U && can_link_error_count==0U);
    CHECK(time_init_calls==step);CheckTimeLost(CAN_TIME_LOSS_HAL);
    calls=time_init_calls;resets=timeout_resets;
    fail_time_at=0U;CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    Queue(8U,0U,1U);CanLink_Poll();CHECK(can_link_rx_count==1U && send_calls==1U);
    QueueQuery(10U,0U);CanLink_Poll();ExpectTime(10U,0U,2U);
    CHECK(time_init_calls==calls && timeout_resets==resets && start_calls==1U);
    CHECK(can_link_error_count==0U);CheckTimeLost(CAN_TIME_LOSS_HAL);
}
static void TimeBadEffect(unsigned step)
{
    bad_time_at=step;CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state==1U && start_calls==1U && time_init_calls==6U);
    CHECK((can_link_time_diag.reason_flags&CAN_TIME_LOSS_CONFIG)!=0U);
    CHECK(can_link_time_diag.state==CAN_TIME_LOST && can_link_error_count==0U);
    QueueQuery(11U,0U);CanLink_Poll();ExpectTime(11U,0U,UINT32_MAX);
}
static void TimeInitBoundary(unsigned mode)
{
    if(mode==0U)host_can_registers.IR=FDCAN_IR_TOO;
    else if(mode==1U)host_can_registers.RXF0S=1U;
    else { time_hook_site=TIME_HOOK_START;time_hook_action=1U; }
    CanLink_Init(ReadStatus,RequestStop,CheckTargets,ReadBoot);
    CHECK(can_link_diag.state==1U && start_calls==1U && time_init_calls==6U);
    CheckTimeLost(mode==1U?CAN_TIME_LOSS_CONFIG:CAN_TIME_LOSS_TIMEOUT);
    if(mode==2U)CHECK(time_hook_fired==1U);
    if(mode!=1U)CHECK((host_can_registers.IR&FDCAN_IR_TOO)!=0U);
    QueueQuery(10U,0U);CanLink_Poll();ExpectTime(10U,0U,2U);
    CHECK(timeout_resets==1U && can_link_error_count==0U);
}
static void TimeConfiguration(unsigned mode)
{
    unsigned resets;
    Ready();resets=timeout_resets;
    switch(mode) {
    case 0U:host_can_registers.TSCC^=1U<<FDCAN_TSCC_TCP_Pos;break;
    case 1U:host_can_registers.TSCC=0U;break;
    case 2U:host_can_registers.TOCC^=1U<<FDCAN_TOCC_TOP_Pos;break;
    case 3U:host_can_registers.TOCC|=1U<<FDCAN_TOCC_TOS_Pos;break;
    case 4U:host_can_registers.TOCC&=~FDCAN_TOCC_ETOC;break;
    case 5U:host_can_registers.NBTP^=1U<<FDCAN_NBTP_NBRP_Pos;break;
    case 6U:host_can_registers.NBTP^=1U<<FDCAN_NBTP_NSJW_Pos;break;
    case 7U:host_can_registers.NBTP^=1U<<FDCAN_NBTP_NTSEG1_Pos;break;
    case 8U:host_can_registers.NBTP^=1U<<FDCAN_NBTP_NTSEG2_Pos;break;
    case 9U:host_can_registers.CCCR|=FDCAN_CCCR_INIT;break;
    case 10U:host_can_registers.CCCR|=FDCAN_CCCR_CSR;break;
    case 11U:host_can_registers.CCCR|=FDCAN_CCCR_CSA;break;
    case 12U:host_can_registers.CCCR|=FDCAN_CCCR_MON;break;
    case 13U:host_can_registers.CCCR|=FDCAN_CCCR_TEST;break;
    case 14U:host_can_registers.CCCR|=FDCAN_CCCR_ASM;break;
    case 15U:host_can_registers.CCCR|=FDCAN_CCCR_FDOE;break;
    case 16U:host_can_registers.CCCR|=FDCAN_CCCR_BRSE;break;
    case 17U:host_can_registers.IE=1U;break;
    case 18U:host_can_registers.ILE=1U;break;
    case 19U:host_debug_registers.APB1HFZ1|=DBGMCU_APB1HFZ1_DBG_FDCAN;break;
    case 20U:host_rcc_registers.D2CCIP1R=1U<<RCC_D2CCIP1R_FDCANSEL_Pos;break;
    case 21U:kernel_clock_hz=0U;break;
    case 22U:kernel_clock_hz=24000000U;break;
    case 23U:host_ccu_registers.CCFG&=~FDCANCCU_CCFG_BCC;break;
    case 24U:host_ccu_registers.CCFG|=1U<<FDCANCCU_CCFG_CDIV_Pos;break;
    case 25U:host_ccu_registers.CCFG|=FDCANCCU_CCFG_SWR;break;
    default:CHECK(0);break;
    }
    QueueQuery(10U,0U);CanLink_Poll();ExpectTime(10U,0U,2U);
    CheckTimeLost(CAN_TIME_LOSS_CONFIG);
    CHECK(timeout_resets==resets && can_link_error_count==0U && can_link_diag.state==1U);
}
static void TimeGeometry(unsigned mode)
{
    Ready();Queue(8U,0U,1U);
    switch(mode) {
    case 0U:host_can_registers.RXF0C^=1U<<FDCAN_RXF0C_F0S_Pos;break;
    case 1U:host_can_registers.RXF0C^=1U<<FDCAN_RXF0C_F0SA_Pos;break;
    case 2U:host_can_registers.RXF0C|=FDCAN_RXF0C_F0OM;break;
    case 3U:host_can_registers.RXESC|=1U;break;
    case 4U:hfdcan1.Init.RxFifo0ElmtsNbr=9U;break;
    case 5U:hfdcan1.Init.RxFifo0ElmtSize=1U;break;
    case 6U:hfdcan1.msgRam.RxFIFO0SA+=4U;break;
    case 7U:fifo_fill=9U;break;
    default:CHECK(0);break;
    }
    CanLink_Poll();CheckTimeLost(CAN_TIME_LOSS_CONFIG);
    CHECK(can_link_diag.state==2U && can_link_error_count==1U);
    CHECK(can_link_diag.error_flags==CAN_LINK_ERROR_CONFIG && receive_calls==0U && send_calls==0U);
    CHECK(host_can_registers.RXF0A==UINT32_MAX);
}
static void TimeEdges(unsigned mode)
{
    unsigned resets;
    static const unsigned site[]={TIME_HOOK_CLOCK,TIME_HOOK_RESET_BEFORE,TIME_HOOK_RESET_AFTER,
        TIME_HOOK_FILL,TIME_HOOK_RX,TIME_HOOK_CLOCK,TIME_HOOK_CLOCK,TIME_HOOK_RX,
        TIME_HOOK_RX,TIME_HOOK_RX,TIME_HOOK_RX,TIME_HOOK_RX,TIME_HOOK_CLOCK,TIME_HOOK_RESET_AFTER};
    static const unsigned skip[]={0U,0U,0U,0U,0U,2U,3U,0U,0U,0U,0U,0U,0U,0U};
    static const unsigned action[]={1U,1U,1U,1U,1U,1U,1U,4U,5U,6U,7U,3U,2U,2U};
    uint32_t reason;
    CHECK(mode<sizeof(site)/sizeof(site[0]));Ready();
    time_hook_site=site[mode];time_hook_skip=skip[mode];time_hook_action=action[mode];
    QueueQuery(11U,0U);SetFrameClock(400U,390U);CanLink_Poll();
    CHECK(time_hook_fired==1U);ExpectTime(11U,0U,UINT32_MAX);
    reason=action[mode]==4U?CAN_TIME_LOSS_RX_LOST:
        (action[mode]>=5U?CAN_TIME_LOSS_LINK:
        (action[mode]==3U?CAN_TIME_LOSS_CONFIG:CAN_TIME_LOSS_TIMEOUT));
    CheckTimeLost(reason);CHECK(can_link_error_count==0U);
    resets=timeout_resets;fifo_fill=0U;CanLink_Poll();CHECK(timeout_resets==resets);
}
static void TimeRuntimeFailure(unsigned mode)
{
    unsigned resets;
    uint32_t reason=CAN_TIME_LOSS_HAL;
    Ready();Queue(8U,0U,1U);
    switch(mode) {
    case 0U:fail_runtime_reset=1U;break;
    case 1U:receive_result=HAL_ERROR;break;
    case 2U:host_can_registers.IR=FDCAN_IR_RF0L;reason=CAN_TIME_LOSS_RX_LOST;break;
    case 3U:host_can_registers.IR=FDCAN_IR_MRAF;reason=CAN_TIME_LOSS_LINK;break;
    case 4U:host_can_registers.IR=FDCAN_IR_WDI;reason=CAN_TIME_LOSS_LINK;break;
    case 5U:host_can_registers.IR=FDCAN_IR_BO;reason=CAN_TIME_LOSS_LINK;break;
    case 6U:host_can_registers.PSR=FDCAN_PSR_BO;reason=CAN_TIME_LOSS_LINK;break;
    default:CHECK(0);break;
    }
    CanLink_Poll();CheckTimeLost(reason);
    CHECK(can_link_error_count==(mode==0U?0U:1U));
    if(mode>=3U)CHECK(receive_calls==0U && send_calls==0U);
    resets=timeout_resets;CanLink_Poll();CHECK(timeout_resets==resets);
}
static void TimeNonfatal(void)
{
    Ready();Queue(8U,0U,1U);SetFrameClock(3U,1U);
    host_can_registers.IR=FDCAN_IR_PEA;CanLink_Poll();
    CHECK(can_link_error_count==1U && can_link_diag.error_flags==CAN_LINK_ERROR_PROTOCOL);
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_time_diag.last_rx_age_us==64U);
    Queue(8U,0U,1U);fifo_free=0U;CanLink_Poll();
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_error_count==2U);
    Queue(8U,0U,1U);fifo_free=32U;send_result=HAL_ERROR;CanLink_Poll();
    CHECK(can_link_time_diag.state==CAN_TIME_TRACKING && can_link_error_count==3U);
    CHECK(can_link_time_diag.reason_flags==0U && timeout_resets==4U);
}
static void TimeCoreUntouched(void)
{
    unsigned i;
    AxisCommandCore before;
    Ready();use_core=1U;AxisCommand_Init(&real_core,&target_limits);
    (void)PublishFixture(AXIS_CMD_START,1U);
    AxisCommand_Tick(&real_core,1U,0U,1U);
    CHECK(real_core.active_valid==1U && real_core.applied_seq!=0U);
    (void)PublishFixture(AXIS_CMD_UPDATE,2U);before=real_core;
    for(i=0U;i<4U;++i) {
        if(i==2U)host_can_registers.IR=FDCAN_IR_TOO;
        QueueQuery((uint8_t)(10U+i%2U),(uint8_t)i);SetFrameClock(300U,100U);CanLink_Poll();
        CHECK(memcmp(&before,&real_core,sizeof(before))==0);
    }
    CHECK(status_reads==0U && stop_calls==0U && target_calls==0U && can_link_error_count==0U);
}
static void StopTimeCompatible(void)
{
    Ready();host_can_registers.IR=FDCAN_IR_TOO;
    Queue(8U,0U,1U);CanLink_Poll();CHECK(can_link_rx_count==1U && send_calls==1U);
    QueueStop(1U);CanLink_Poll();ExpectStop(1U,1U);
    observed_status.stop_applied_ticket=41U;
    QueueStop(1U);CanLink_Poll();ExpectStop(1U,2U);CHECK(stop_calls==1U);
    QueueTarget(2U,100U,1000U);CanLink_Poll();ExpectTarget(1U);
    QueueQuery(4U,3U);CanLink_Poll();ExpectTime(4U,3U,0x806U);
    QueueQuery(10U,4U);CanLink_Poll();ExpectTime(10U,4U,2U);
    CHECK(target_calls==1U && can_link_error_count==0U && timeout_resets==1U);
    CheckTimeLost(CAN_TIME_LOSS_TIMEOUT);
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
    else if (!strcmp(argv[1], "stop-target-retained")) StopTargetRetained();
    else if (!strcmp(argv[1], "stop-boot-retained")) StopBootRetained();
    else if (!strncmp(argv[1], "boot-state-",11)) BootQueries((uint32_t)atoi(argv[1]+11));
    else if (!strcmp(argv[1], "boot-rejects")) BootRejects();
    else if (!strcmp(argv[1], "boot-dlc")) BootDlc();
    else if (!strcmp(argv[1], "boot-missing")) BootMissing();
    else if (!strcmp(argv[1], "boot-bad-provider")) BootBadProvider();
    else if (!strncmp(argv[1], "boot-failure-",13)) BootFailure((unsigned)atoi(argv[1]+13));
    else if (!strcmp(argv[1], "target-valid")) TargetValid();
    else if (!strcmp(argv[1], "target-rejects")) TargetRejects();
    else if (!strcmp(argv[1], "target-dlc")) TargetDlc();
    else if (!strcmp(argv[1], "target-missing")) TargetMissing();
    else if (!strcmp(argv[1], "target-bad-provider")) TargetBadProvider();
    else if (!strcmp(argv[1], "target-core")) TargetCore();
    else if (!strncmp(argv[1], "target-failure-",15)) TargetFailure((unsigned)atoi(argv[1]+15));
    else if (!strncmp(argv[1], "bad-config-", 11)) BadConfig((unsigned)atoi(argv[1] + 11));
    else if (!strncmp(argv[1], "init-fail-", 10)) InitFailure((unsigned)atoi(argv[1] + 10));
    else if (!strcmp(argv[1], "time-normal")) TimeNormal();
    else if (!strcmp(argv[1], "time-metadata")) TimeMetadata();
    else if (!strcmp(argv[1], "time-progress")) TimeBoundedProgress();
    else if (!strcmp(argv[1], "time-watch")) TimeWatch();
    else if (!strcmp(argv[1], "time-nonfatal")) TimeNonfatal();
    else if (!strcmp(argv[1], "time-core")) TimeCoreUntouched();
    else if (!strcmp(argv[1], "stop-time-compatible")) StopTimeCompatible();
    else if (!strncmp(argv[1], "time-sticky-",12)) TimeSticky((unsigned)atoi(argv[1]+12));
    else if (!strncmp(argv[1], "time-init-fail-",15)) TimeInitFailure((unsigned)atoi(argv[1]+15));
    else if (!strncmp(argv[1], "time-bad-effect-",16)) TimeBadEffect((unsigned)atoi(argv[1]+16));
    else if (!strncmp(argv[1], "time-init-boundary-",19)) TimeInitBoundary((unsigned)atoi(argv[1]+19));
    else if (!strncmp(argv[1], "time-config-",12)) TimeConfiguration((unsigned)atoi(argv[1]+12));
    else if (!strncmp(argv[1], "time-geometry-",14)) TimeGeometry((unsigned)atoi(argv[1]+14));
    else if (!strncmp(argv[1], "time-edge-",10)) TimeEdges((unsigned)atoi(argv[1]+10));
    else if (!strncmp(argv[1], "time-failure-",13)) TimeRuntimeFailure((unsigned)atoi(argv[1]+13));
    else CHECK(0);
    if(strncmp(argv[1],"stop-",5)) CHECK(stop_calls==0U);
    CHECK(ignored_stop_calls==0U && ignored_target_calls==0U && ignored_boot_reads==0U);
    if(strncmp(argv[1],"boot-",5) && strcmp(argv[1],"stop-boot-retained"))CHECK(boot_reads==0U);
    if(strncmp(argv[1],"target-",7) && strcmp(argv[1],"stop-target-retained") && strcmp(argv[1],"stop-time-compatible"))CHECK(target_calls==0U);
    printf("CAN service %s: %u assertions PASS\n", argv[1], checks);
    return 0;
}
