#include "board_config.h"
#include "can_link.h"

#if AXIS_COMMAND_BENCH_ENABLE
#include "fdcan.h"
#include "can_link_protocol.h"

/* Age proof: blocking FIFO0 has exactly 8 slots and every nonempty Poll removes
 * one frame or loses trust. Nine service intervals plus 16 ticks (512 us) for
 * a successful 8-byte Classic frame must fit one 16-bit timestamp revolution.
 * FIFO geometry, progress policy, nominal bit timing and this bound are coupled. */
typedef char CanTime_age_bound[(CAN_TIME_TICK_US == 32U &&
    CAN_TIME_SERVICE_TICKS != 0U && CAN_TIME_SERVICE_TICKS < 65536U &&
    (9U * CAN_TIME_SERVICE_TICKS + 16U) < 65536U) ? 1 : -1];

#define LINK_ERROR_IR (FDCAN_IR_RF0L | FDCAN_IR_MRAF | FDCAN_IR_WDI | \
                       FDCAN_IR_PEA | FDCAN_IR_PED | FDCAN_IR_BO | \
                       FDCAN_IR_EW | FDCAN_IR_EP | FDCAN_IR_ELO)
#define LINK_POLLED_IR (LINK_ERROR_IR | FDCAN_IR_RF0N | FDCAN_IR_RF0F | \
                        FDCAN_IR_RF0W | FDCAN_IR_TC | FDCAN_IR_TCF | FDCAN_IR_TFE)

volatile uint32_t can_link_rx_count;
volatile uint32_t can_query_rx_count;
volatile uint32_t can_link_error_count;
volatile CanLinkDiagnostics can_link_diag;
volatile CanLinkTimeDiagnostics can_link_time_diag = {
    CAN_TIME_NOT_READY, 0U, UINT32_MAX
};
/* Authoritative appTask-only state. Debugger mirrors are never inputs. */
static uint32_t time_state = CAN_TIME_NOT_READY;
static uint32_t time_reason_flags;
static uint32_t last_rx_age_us = UINT32_MAX;
static uint8_t init_attempted;
static CanLinkStatusReader status_reader;
static CanLinkStopRequester stop_requester;
static CanLinkTargetChecker target_checker;
static CanLinkBootReader boot_reader;
/* appTask-owned single retained result, no ISR access and no dynamic memory.
 * Sole producer + nonzero increasing IDs bounds Stop calls to UINT32_MAX,
 * preventing reuse of a saturated core ticket. Revisit if adding producers. */
static uint32_t last_stop_request_id;
static uint32_t last_stop_ticket;

static void PublishTimeDiagnostics(void)
{
    can_link_time_diag.state = time_state;
    can_link_time_diag.reason_flags = time_reason_flags;
    can_link_time_diag.last_rx_age_us = last_rx_age_us;
}

static void TimeLost(uint32_t reason)
{
    time_state = CAN_TIME_LOST;
    time_reason_flags |= reason;
    last_rx_age_us = UINT32_MAX;
    PublishTimeDiagnostics();
}

static uint32_t TimeConfigurationValid(void)
{
    const uint32_t nbtp_mask = FDCAN_NBTP_NSJW | FDCAN_NBTP_NBRP |
                               FDCAN_NBTP_NTSEG1 | FDCAN_NBTP_NTSEG2;
    const uint32_t forbidden_mode = FDCAN_CCCR_INIT | FDCAN_CCCR_CSR |
        FDCAN_CCCR_CSA | FDCAN_CCCR_MON | FDCAN_CCCR_TEST | FDCAN_CCCR_ASM |
        FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
    return (hfdcan1.Instance == FDCAN1) &&
        ((hfdcan1.Instance->NBTP & nbtp_mask) == 0x00040601UL) &&
        ((hfdcan1.Instance->TSCC & (FDCAN_TSCC_TCP | FDCAN_TSCC_TSS)) ==
            (FDCAN_TIMESTAMP_PRESC_16 | FDCAN_TIMESTAMP_INTERNAL)) &&
        ((hfdcan1.Instance->TOCC & (FDCAN_TOCC_TOP | FDCAN_TOCC_TOS |
                                   FDCAN_TOCC_ETOC)) ==
            ((CAN_TIME_SERVICE_TICKS << FDCAN_TOCC_TOP_Pos) |
             FDCAN_TIMEOUT_CONTINUOUS | FDCAN_TOCC_ETOC)) &&
        ((hfdcan1.Instance->CCCR & forbidden_mode) == 0U) &&
        (hfdcan1.Instance->IE == 0U) && (hfdcan1.Instance->ILE == 0U) &&
        ((DBGMCU->APB1HFZ1 & DBGMCU_APB1HFZ1_DBG_FDCAN) == 0U) &&
        /* HAL's CCU bypass uses BCC=1 and CDIV=0 for kernel-clock DIV1. */
        ((FDCAN_CCU->CCFG & (FDCANCCU_CCFG_BCC | FDCANCCU_CCFG_CDIV |
                             FDCANCCU_CCFG_SWR)) == FDCANCCU_CCFG_BCC) &&
        (__HAL_RCC_GET_FDCAN_SOURCE() == RCC_FDCANCLKSOURCE_HSE) &&
        (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) == 25000000U);
}

static uint32_t CheckTime(void)
{
    uint32_t reasons = 0U;
    uint32_t ir;
    if (time_state != CAN_TIME_TRACKING) return 0U;
    if (TimeConfigurationValid() == 0U) reasons |= CAN_TIME_LOSS_CONFIG;
    /* Also catch loss/fatal events arising after PollErrors. Only that owner
     * clears/counts old link events; observing them here changes time only. */
    ir = hfdcan1.Instance->IR;
    if ((ir & FDCAN_IR_RF0L) != 0U) reasons |= CAN_TIME_LOSS_RX_LOST;
    if ((ir & (FDCAN_IR_MRAF | FDCAN_IR_WDI | FDCAN_IR_BO)) != 0U)
        reasons |= CAN_TIME_LOSS_LINK;
    /* TOO is sticky and is NEVER cleared by this service, even at Init.
     * Reading zero TOCV is also conservative failure, including boundary races. */
    if (((ir & FDCAN_IR_TOO) != 0U) ||
        ((hfdcan1.Instance->TOCV & FDCAN_TOCV_TOC) == 0U))
        reasons |= CAN_TIME_LOSS_TIMEOUT;
    if (reasons != 0U) TimeLost(reasons);
    return (uint32_t)(time_state == CAN_TIME_TRACKING);
}

static void ReloadServiceTimer(void)
{
    if (CheckTime() != 0U)
    {
        if (HAL_FDCAN_ResetTimeoutCounter(&hfdcan1) != HAL_OK)
            TimeLost(CAN_TIME_LOSS_HAL);
        else
            (void)CheckTime(); /* Reloading TOCV cannot erase a sticky TOO. */
    }
    PublishTimeDiagnostics();
}

static void ObserveRxAge(uint32_t rx_timestamp)
{
    uint32_t age;
    last_rx_age_us = UINT32_MAX;
    if (CheckTime() != 0U)
    {
        age = (uint32_t)(uint16_t)(hfdcan1.Instance->TSCV - rx_timestamp) *
              CAN_TIME_TICK_US;
        if (CheckTime() != 0U) last_rx_age_us = age;
    }
    PublishTimeDiagnostics();
}

static void LinkError(uint32_t flags)
{
    can_link_error_count++;
    can_link_diag.error_flags |= flags;
}

void CanLink_Init(CanLinkStatusReader read_status, CanLinkStopRequester request_stop,
                  CanLinkTargetChecker check_targets, CanLinkBootReader read_boot_identity)
{
    FDCAN_FilterTypeDef filter = {0};
    if (init_attempted != 0U) return;
    init_attempted = 1U;
    PublishTimeDiagnostics();
    if ((read_status == 0) || (request_stop == 0) || (check_targets == 0) ||
        (read_boot_identity == 0))
    {
        LinkError(CAN_LINK_ERROR_CONFIG);
        can_link_diag.state = 2U;
        TimeLost(CAN_TIME_LOSS_CONFIG);
        return;
    }
    status_reader = read_status;
    stop_requester = request_stop;
    target_checker = check_targets;
    boot_reader = read_boot_identity;
    /* Check generated geometry before directly peeking the FIFO header below.
     * This service assumes sole FDCAN1 use and an 8-byte blocking FIFO. */
    if ((hfdcan1.Instance != FDCAN1) ||
        (hfdcan1.State != HAL_FDCAN_STATE_READY) ||
        (hfdcan1.Init.FrameFormat != FDCAN_FRAME_CLASSIC) ||
        (hfdcan1.Init.Mode != FDCAN_MODE_NORMAL) ||
        (hfdcan1.Init.AutoRetransmission != DISABLE) ||
        (hfdcan1.Init.MessageRAMOffset != 0U) ||
        (hfdcan1.Init.StdFiltersNbr != 1U) ||
        (hfdcan1.Init.ExtFiltersNbr != 0U) ||
        (hfdcan1.Init.RxFifo0ElmtsNbr != 8U) ||
        (hfdcan1.Init.RxFifo0ElmtSize != FDCAN_DATA_BYTES_8) ||
        (hfdcan1.Init.RxFifo1ElmtsNbr != 0U) ||
        (hfdcan1.Init.RxBuffersNbr != 0U) ||
        (hfdcan1.Init.TxEventsNbr != 0U) ||
        (hfdcan1.Init.TxBuffersNbr != 0U) ||
        (hfdcan1.Init.TxFifoQueueElmtsNbr != 32U) ||
        (hfdcan1.Init.TxElmtSize != FDCAN_DATA_BYTES_8) ||
        (hfdcan1.msgRam.RxFIFO0SA != (SRAMCAN_BASE + 4U)) ||
        (hfdcan1.msgRam.EndAddress != (SRAMCAN_BASE + 644U)) ||
        (hfdcan1.Instance->IE != 0U) || (hfdcan1.Instance->ILE != 0U))
    {
        LinkError(CAN_LINK_ERROR_CONFIG);
        can_link_diag.state = 2U;
        TimeLost(CAN_TIME_LOSS_CONFIG);
        return;
    }
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = CAN_LINK_REQUEST_ID;
    filter.FilterID2 = 0x7FFU;
    if ((HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK) ||
        (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT,
             FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) ||
        (HAL_FDCAN_ConfigRxFifoOverwrite(&hfdcan1, FDCAN_RX_FIFO0,
             FDCAN_RX_FIFO_BLOCKING) != HAL_OK))
    {
        LinkError(CAN_LINK_ERROR_INIT);
        can_link_diag.state = 2U;
        TimeLost(CAN_TIME_LOSS_LINK);
        return;
    }
    /* READY alone does not prove that reinitializing hardware had no old RX.
     * Resetting TSCV is safe as an age origin only with an empty receive FIFO. */
    if ((hfdcan1.Instance->RXF0S & FDCAN_RXF0S_F0FL) != 0U)
        TimeLost(CAN_TIME_LOSS_CONFIG);
    __HAL_DBGMCU_UnFreeze_FDCAN();
    if ((HAL_FDCAN_ConfigTimestampCounter(&hfdcan1, FDCAN_TIMESTAMP_PRESC_16) != HAL_OK) ||
        (HAL_FDCAN_EnableTimestampCounter(&hfdcan1, FDCAN_TIMESTAMP_INTERNAL) != HAL_OK) ||
        (HAL_FDCAN_ConfigTimeoutCounter(&hfdcan1, FDCAN_TIMEOUT_CONTINUOUS,
                                       CAN_TIME_SERVICE_TICKS) != HAL_OK) ||
        (HAL_FDCAN_EnableTimeoutCounter(&hfdcan1) != HAL_OK) ||
        (HAL_FDCAN_ResetTimestampCounter(&hfdcan1) != HAL_OK) ||
        (HAL_FDCAN_ResetTimeoutCounter(&hfdcan1) != HAL_OK))
        TimeLost(CAN_TIME_LOSS_HAL);
    /* Time setup failure must not prevent diagnostic communication. */
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        LinkError(CAN_LINK_ERROR_INIT);
        can_link_diag.state = 2U;
        TimeLost(CAN_TIME_LOSS_LINK);
        return;
    }
    can_link_diag.state = 1U;
    if (time_state != CAN_TIME_LOST)
    {
        time_state = CAN_TIME_TRACKING;
        (void)CheckTime();
    }
    PublishTimeDiagnostics();
}

static void PollErrors(void)
{
    uint32_t ir = hfdcan1.Instance->IR & LINK_POLLED_IR;
    uint32_t psr = hfdcan1.Instance->PSR; /* LEC is read once by this owner. */
    uint32_t flags = 0U;
    if (ir != 0U) __HAL_FDCAN_CLEAR_FLAG(&hfdcan1, ir); /* W1C, not RMW */
    if ((ir & FDCAN_IR_RF0L) != 0U)
    {
        can_link_diag.rx_lost_events++;
        flags |= CAN_LINK_ERROR_RX_LOST;
        TimeLost(CAN_TIME_LOSS_RX_LOST);
    }
    if ((ir & (FDCAN_IR_PEA | FDCAN_IR_PED | FDCAN_IR_EW |
               FDCAN_IR_EP | FDCAN_IR_ELO)) != 0U)
    {
        can_link_diag.protocol_error_events++;
        flags |= CAN_LINK_ERROR_PROTOCOL;
    }
    if ((ir & (FDCAN_IR_MRAF | FDCAN_IR_WDI)) != 0U)
    {
        flags |= CAN_LINK_ERROR_RAM;
        TimeLost(CAN_TIME_LOSS_LINK);
        can_link_diag.state = 2U;
    }
    if (((psr & FDCAN_PSR_BO) != 0U) || ((ir & FDCAN_IR_BO) != 0U))
    {
        flags |= CAN_LINK_ERROR_BUS_OFF;
        TimeLost(CAN_TIME_LOSS_LINK);
        can_link_diag.state = 3U; /* no automatic recovery or motor command */
    }
    if (flags != 0U)
    {
        uint32_t ecr = hfdcan1.Instance->ECR;
        can_link_diag.last_ir = ir;
        can_link_diag.last_lec = psr & FDCAN_PSR_LEC;
        can_link_diag.tx_error_count = (ecr & FDCAN_ECR_TEC) >> FDCAN_ECR_TEC_Pos;
        can_link_diag.rx_error_count = (ecr & FDCAN_ECR_REC) >> FDCAN_ECR_REC_Pos;
        LinkError(flags);
    }
}

static uint32_t QueryValue(uint8_t opcode)
{
    CanLinkReadStatus snapshot;
    uint32_t flags;
    if (opcode == CAN_QUERY_INFO) return CAN_QUERY_INFO_VALUE;
    if (opcode == CAN_QUERY_BUILD) return CAN_QUERY_BUILD_REVISION;
    if (opcode == CAN_QUERY_TIME_STATE) return time_state;
    if (opcode == CAN_QUERY_RX_AGE_US) return last_rx_age_us;
    if ((opcode >= CAN_QUERY_BOOT_STATE) && (opcode <= CAN_QUERY_BOOT_WORD3))
    {
        uint32_t words[4] = {0U, 0U, 0U, 0U};
        uint32_t state = boot_reader(words);
        if (state > CAN_BOOT_STATE_FAILED)
        {
            state = CAN_BOOT_STATE_FAILED;
            LinkError(CAN_LINK_ERROR_CONFIG);
        }
        if (opcode == CAN_QUERY_BOOT_STATE) return state;
        /* Never expose partial/failed identity, even if the provider filled it.
         * Four paged replies are observations, not a validated control session. */
        return state == CAN_BOOT_STATE_VALID ? words[opcode - CAN_QUERY_BOOT_WORD0] : 0U;
    }
    status_reader(&snapshot);
    if (opcode == CAN_QUERY_FAULT) return snapshot.fault_mask;
    flags = (snapshot.configured != 0U ? 1U : 0U) |
            (snapshot.permit != 0U ? 2U : 0U) |
            (snapshot.output_enabled != 0U ? 4U : 0U) |
            (AXIS_COMMAND_BENCH_ENABLE != 0 ? 8U : 0U) |
            (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_DISABLED ? 16U : 0U);
    /* axis_status's low byte is state; upper bits are inhibit observations.
     * A fault can precede the next permit mirror update. No control authority. */
    return (snapshot.status_word & 0xFFU) | (flags << 8U) |
           (((snapshot.status_word >> 8U) & 0xFFFFU) << 16U);
}

/* Returns a historical result for duplicate IDs. A new stop intent must have
 * a larger ID. PROCESSED is software disarm completion, not measured standstill.
 * Call BEFORE checking TX space: a lost reply must never prevent a stop. */
static uint8_t HandleStop(uint32_t request_id)
{
    CanLinkReadStatus snapshot;
    uint32_t ticket = 0U;
    if (last_stop_request_id != 0U)
    {
        if (request_id < last_stop_request_id) return CAN_STOP_STALE;
        status_reader(&snapshot);
        if (request_id == last_stop_request_id)
            return (snapshot.stop_applied_ticket == last_stop_ticket) ?
                CAN_STOP_PROCESSED : CAN_STOP_ACCEPTED;
        if (snapshot.stop_applied_ticket != last_stop_ticket) return CAN_STOP_BUSY;
    }
    if ((stop_requester(&ticket) == 0U) || (ticket == 0U))
        return CAN_STOP_REJECTED;
    last_stop_request_id = request_id;
    last_stop_ticket = ticket;
    return CAN_STOP_ACCEPTED;
}

void CanLink_Poll(void)
{
    FDCAN_RxHeaderTypeDef rx;
    FDCAN_TxHeaderTypeDef tx = {0};
    CanLinkFrame request;
    CanLinkFrame reply;
    CanLinkTargetPair pair;
    uint32_t index;
    uint32_t fill;
    uint32_t stop_id;
    uint8_t opcode;
    uint8_t check_result;
    volatile const uint32_t *element;
    PublishTimeDiagnostics();
    if (can_link_diag.state != 1U) return;
    PollErrors();
    if (can_link_diag.state != 1U) return;
    ReloadServiceTimer();
    fill = HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0);
    if (fill == 0U)
    {
        (void)CheckTime();
        return;
    }

    /* HAL 1.12.1 GetRxMessage copies DLCtoBytes[DLC] without clamping to
     * configured element size. Classic frames may carry DLC 9..15. Reject
     * non-8 DLC BEFORE HAL copies, avoiding both buffer and slot over-read.
     * Blocking FIFO + sole consumer keeps this element stable until ack. */
    index = (hfdcan1.Instance->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    if ((fill > 8U) || (index >= 8U) ||
        ((hfdcan1.Instance->RXF0C & (FDCAN_RXF0C_F0OM | FDCAN_RXF0C_F0S |
                                    FDCAN_RXF0C_F0SA)) !=
            ((8U << FDCAN_RXF0C_F0S_Pos) | (1U << FDCAN_RXF0C_F0SA_Pos))) ||
        ((hfdcan1.Instance->RXESC & FDCAN_RXESC_F0DS) != 0U) ||
        (hfdcan1.Init.RxFifo0ElmtsNbr != 8U) ||
        (hfdcan1.Init.RxFifo0ElmtSize != FDCAN_DATA_BYTES_8) ||
        (hfdcan1.msgRam.RxFIFO0SA != (SRAMCAN_BASE + 4U)))
    {
        LinkError(CAN_LINK_ERROR_CONFIG);
        can_link_diag.state = 2U;
        TimeLost(CAN_TIME_LOSS_CONFIG);
        return;
    }
    element = (volatile const uint32_t *)(uintptr_t)
        (hfdcan1.msgRam.RxFIFO0SA + index * 16U);
    if (((element[1] >> 16U) & 0xFU) != 8U)
    {
        hfdcan1.Instance->RXF0A = index;
        last_rx_age_us = UINT32_MAX;
        (void)CheckTime();
        PublishTimeDiagnostics();
        can_link_diag.rejected_count++;
        return;
    }
    if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rx, request.data) != HAL_OK)
    {
        LinkError(CAN_LINK_ERROR_HAL);
        TimeLost(CAN_TIME_LOSS_HAL); /* No FIFO progress can be assumed. */
        return;
    }
    ObserveRxAge(rx.RxTimestamp);
    request.id = rx.Identifier;
    request.extended = (uint8_t)(rx.IdType != FDCAN_STANDARD_ID);
    request.remote = (uint8_t)(rx.RxFrameType != FDCAN_DATA_FRAME);
    request.fd = (uint8_t)(rx.FDFormat != FDCAN_CLASSIC_CAN);
    request.brs = (uint8_t)(rx.BitRateSwitch != FDCAN_BRS_OFF);
    request.length = (uint8_t)((rx.DataLength == FDCAN_DLC_BYTES_8) ? 8U : 0U);
    if (CanLinkProtocol_MakeReply(&request, &reply) != 0)
    {
        can_link_rx_count++;
    }
    else
    {
        opcode = CanLinkProtocol_QueryOpcode(&request);
        if (opcode != 0U)
        {
            /* Queries never submit/renew motor commands or clear faults.
             * The healthy service itself reloads its progress timer above. */
            (void)CanLinkProtocol_MakeQueryReply(&request, QueryValue(opcode), &reply);
            can_query_rx_count++;
        }
        else
        {
            stop_id = CanLinkProtocol_StopRequestId(&request);
            if (stop_id != 0U)
            {
                (void)CanLinkProtocol_MakeStopReply(&request, HandleStop(stop_id), &reply);
            }
            else if (CanLinkProtocol_ReadTargetCheck(&request, &pair) != 0)
            {
                /* Same complete private pair; never submit or stamp a lease.
                 * Old/repeated frames only repeat this read-only precheck. */
                check_result = target_checker(pair.speed_rpm, pair.iq_limit_ma);
                if ((check_result < CAN_TARGET_CHECK_OK) ||
                    (check_result > CAN_TARGET_CHECK_REJECTED))
                {
                    check_result = CAN_TARGET_CHECK_REJECTED;
                    LinkError(CAN_LINK_ERROR_CONFIG);
                }
                (void)CanLinkProtocol_MakeTargetCheckReply(&request, check_result, &reply);
            }
            else
            {
                can_link_diag.rejected_count++;
                return;
            }
        }
    }
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U)
    {
        can_link_diag.tx_busy_count++;
        LinkError(CAN_LINK_ERROR_TX_BUSY);
        return; /* drop reply; no wait, retry or software backlog */
    }
    tx.Identifier = reply.id;
    tx.IdType = FDCAN_STANDARD_ID;
    tx.TxFrameType = FDCAN_DATA_FRAME;
    tx.DataLength = FDCAN_DLC_BYTES_8;
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch = FDCAN_BRS_OFF;
    tx.FDFormat = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx, reply.data) != HAL_OK)
    {
        LinkError(CAN_LINK_ERROR_HAL);
        return;
    }
    can_link_diag.tx_enqueued_count++;
}
#endif /* AXIS_COMMAND_BENCH_ENABLE */
