#include "board_config.h"
#include "can_link.h"

#if AXIS_COMMAND_BENCH_ENABLE
#include "fdcan.h"
#include "can_link_protocol.h"

#define LINK_ERROR_IR (FDCAN_IR_RF0L | FDCAN_IR_MRAF | FDCAN_IR_WDI | \
                       FDCAN_IR_PEA | FDCAN_IR_PED | FDCAN_IR_BO | \
                       FDCAN_IR_EW | FDCAN_IR_EP | FDCAN_IR_ELO)
#define LINK_POLLED_IR (LINK_ERROR_IR | FDCAN_IR_RF0N | FDCAN_IR_RF0F | \
                        FDCAN_IR_RF0W | FDCAN_IR_TC | FDCAN_IR_TCF | FDCAN_IR_TFE)

volatile uint32_t can_link_rx_count;
volatile uint32_t can_query_rx_count;
volatile uint32_t can_link_error_count;
volatile CanLinkDiagnostics can_link_diag;
static uint8_t init_attempted;
static CanLinkStatusReader status_reader;

static void LinkError(uint32_t flags)
{
    can_link_error_count++;
    can_link_diag.error_flags |= flags;
}

void CanLink_Init(CanLinkStatusReader read_status)
{
    FDCAN_FilterTypeDef filter = {0};
    if (init_attempted != 0U) return;
    init_attempted = 1U;
    if (read_status == 0)
    {
        LinkError(CAN_LINK_ERROR_CONFIG);
        can_link_diag.state = 2U;
        return;
    }
    status_reader = read_status;
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
             FDCAN_RX_FIFO_BLOCKING) != HAL_OK) ||
        (HAL_FDCAN_Start(&hfdcan1) != HAL_OK))
    {
        LinkError(CAN_LINK_ERROR_INIT);
        can_link_diag.state = 2U;
        return;
    }
    can_link_diag.state = 1U;
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
        can_link_diag.state = 2U;
    }
    if (((psr & FDCAN_PSR_BO) != 0U) || ((ir & FDCAN_IR_BO) != 0U))
    {
        flags |= CAN_LINK_ERROR_BUS_OFF;
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

void CanLink_Poll(void)
{
    FDCAN_RxHeaderTypeDef rx;
    FDCAN_TxHeaderTypeDef tx = {0};
    CanLinkFrame request;
    CanLinkFrame reply;
    uint32_t index;
    uint8_t opcode;
    volatile const uint32_t *element;
    if (can_link_diag.state != 1U) return;
    PollErrors();
    if (can_link_diag.state != 1U) return;
    if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) == 0U) return;

    /* HAL 1.12.1 GetRxMessage copies DLCtoBytes[DLC] without clamping to
     * configured element size. Classic frames may carry DLC 9..15. Reject
     * non-8 DLC BEFORE HAL copies, avoiding both buffer and slot over-read.
     * Blocking FIFO + sole consumer keeps this element stable until ack. */
    index = (hfdcan1.Instance->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    if ((index >= 8U) || ((hfdcan1.Instance->RXF0C & FDCAN_RXF0C_F0OM) != 0U))
    {
        LinkError(CAN_LINK_ERROR_CONFIG);
        can_link_diag.state = 2U;
        return;
    }
    element = (volatile const uint32_t *)(uintptr_t)
        (hfdcan1.msgRam.RxFIFO0SA + index * 16U);
    if (((element[1] >> 16U) & 0xFU) != 8U)
    {
        hfdcan1.Instance->RXF0A = index;
        can_link_diag.rejected_count++;
        return;
    }
    if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rx, request.data) != HAL_OK)
    {
        LinkError(CAN_LINK_ERROR_HAL);
        return;
    }
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
        if (opcode == 0U)
        {
            can_link_diag.rejected_count++;
            return;
        }
        /* request is a stable private copy; formatting cannot fail after the
         * same validation. No command submission, watchdog feed or fault clear. */
        (void)CanLinkProtocol_MakeQueryReply(&request, QueryValue(opcode), &reply);
        can_query_rx_count++;
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
