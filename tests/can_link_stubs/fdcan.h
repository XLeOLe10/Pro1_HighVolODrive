#ifndef CAN_LINK_HOST_FDCAN_H
#define CAN_LINK_HOST_FDCAN_H
/* HOST-only register/HAL seam. Relevant constants below are copied verbatim
 * from the project's STM32H750 CMSIS/HAL headers, not a CAN bus simulator. */
#include <stdint.h>
#define DISABLE 0U
#define HAL_FDCAN_STATE_READY 1U
#define HAL_FDCAN_STATE_BUSY 2U
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct { volatile uint32_t IR, PSR, ECR, RXF0S, RXF0C, RXF0A, IE, ILE; } FDCAN_GlobalTypeDef;
extern FDCAN_GlobalTypeDef host_can_registers;
extern uint32_t host_message_ram[161];
#define FDCAN1 (&host_can_registers)
#define SRAMCAN_BASE ((uintptr_t)host_message_ram)
typedef struct {
 uint32_t FrameFormat, Mode, AutoRetransmission, MessageRAMOffset;
 uint32_t StdFiltersNbr, ExtFiltersNbr, RxFifo0ElmtsNbr, RxFifo0ElmtSize;
 uint32_t RxFifo1ElmtsNbr, RxBuffersNbr, TxEventsNbr, TxBuffersNbr;
 uint32_t TxFifoQueueElmtsNbr, TxElmtSize;
} FDCAN_InitTypeDef;
typedef struct { uintptr_t RxFIFO0SA, EndAddress; } FDCAN_MsgRamAddressTypeDef;
typedef struct {
 FDCAN_GlobalTypeDef *Instance;
 uint32_t State;
 FDCAN_InitTypeDef Init;
 FDCAN_MsgRamAddressTypeDef msgRam;
} FDCAN_HandleTypeDef;
extern FDCAN_HandleTypeDef hfdcan1;
typedef struct { uint32_t IdType, FilterIndex, FilterType, FilterConfig, FilterID1, FilterID2; } FDCAN_FilterTypeDef;
typedef struct { uint32_t Identifier, IdType, RxFrameType, FDFormat, BitRateSwitch, DataLength; } FDCAN_RxHeaderTypeDef;
typedef struct { uint32_t Identifier, IdType, TxFrameType, DataLength, ErrorStateIndicator, BitRateSwitch, FDFormat, TxEventFifoControl, MessageMarker; } FDCAN_TxHeaderTypeDef;
void HostClearFlag(FDCAN_HandleTypeDef *, uint32_t);
#define __HAL_FDCAN_CLEAR_FLAG(h, flags) HostClearFlag((h), (flags))
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *, const FDCAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *, uint32_t, uint32_t, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_ConfigRxFifoOverwrite(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *, uint32_t, FDCAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *, const FDCAN_TxHeaderTypeDef *, const uint8_t *);
#define FDCAN_BRS_OFF ((uint32_t)0x00000000U)
#define FDCAN_CLASSIC_CAN ((uint32_t)0x00000000U)
#define FDCAN_DATA_BYTES_8  ((uint32_t)0x00000004U)
#define FDCAN_DATA_FRAME   ((uint32_t)0x00000000U)
#define FDCAN_DLC_BYTES_8  ((uint32_t)0x00000008U)
#define FDCAN_ECR_REC             FDCAN_ECR_REC_Msk
#define FDCAN_ECR_REC_Msk         (0x7FUL << FDCAN_ECR_REC_Pos)
#define FDCAN_ECR_REC_Pos         (8U)
#define FDCAN_ECR_TEC             FDCAN_ECR_TEC_Msk
#define FDCAN_ECR_TEC_Msk         (0xFFUL << FDCAN_ECR_TEC_Pos)
#define FDCAN_ECR_TEC_Pos         (0U)
#define FDCAN_ESI_ACTIVE  ((uint32_t)0x00000000U)
#define FDCAN_FILTER_MASK          ((uint32_t)0x00000002U)
#define FDCAN_FILTER_TO_RXFIFO0    ((uint32_t)0x00000001U)
#define FDCAN_FRAME_CLASSIC   ((uint32_t)0x00000000U)
#define FDCAN_IR_BO               FDCAN_IR_BO_Msk
#define FDCAN_IR_BO_Msk           (0x1UL << FDCAN_IR_BO_Pos)
#define FDCAN_IR_BO_Pos           (25U)
#define FDCAN_IR_ELO              FDCAN_IR_ELO_Msk
#define FDCAN_IR_ELO_Msk          (0x1UL << FDCAN_IR_ELO_Pos)
#define FDCAN_IR_ELO_Pos          (22U)
#define FDCAN_IR_EP               FDCAN_IR_EP_Msk
#define FDCAN_IR_EP_Msk           (0x1UL << FDCAN_IR_EP_Pos)
#define FDCAN_IR_EP_Pos           (23U)
#define FDCAN_IR_EW               FDCAN_IR_EW_Msk
#define FDCAN_IR_EW_Msk           (0x1UL << FDCAN_IR_EW_Pos)
#define FDCAN_IR_EW_Pos           (24U)
#define FDCAN_IR_MRAF             FDCAN_IR_MRAF_Msk
#define FDCAN_IR_MRAF_Msk         (0x1UL << FDCAN_IR_MRAF_Pos)
#define FDCAN_IR_MRAF_Pos         (17U)
#define FDCAN_IR_PEA              FDCAN_IR_PEA_Msk
#define FDCAN_IR_PEA_Msk          (0x1UL << FDCAN_IR_PEA_Pos)
#define FDCAN_IR_PEA_Pos          (27U)
#define FDCAN_IR_PED              FDCAN_IR_PED_Msk
#define FDCAN_IR_PED_Msk          (0x1UL << FDCAN_IR_PED_Pos)
#define FDCAN_IR_PED_Pos          (28U)
#define FDCAN_IR_RF0F             FDCAN_IR_RF0F_Msk
#define FDCAN_IR_RF0F_Msk         (0x1UL << FDCAN_IR_RF0F_Pos)
#define FDCAN_IR_RF0F_Pos         (2U)
#define FDCAN_IR_RF0L             FDCAN_IR_RF0L_Msk
#define FDCAN_IR_RF0L_Msk         (0x1UL << FDCAN_IR_RF0L_Pos)
#define FDCAN_IR_RF0L_Pos         (3U)
#define FDCAN_IR_RF0N             FDCAN_IR_RF0N_Msk
#define FDCAN_IR_RF0N_Msk         (0x1UL << FDCAN_IR_RF0N_Pos)
#define FDCAN_IR_RF0N_Pos         (0U)
#define FDCAN_IR_RF0W             FDCAN_IR_RF0W_Msk
#define FDCAN_IR_RF0W_Msk         (0x1UL << FDCAN_IR_RF0W_Pos)
#define FDCAN_IR_RF0W_Pos         (1U)
#define FDCAN_IR_TC               FDCAN_IR_TC_Msk
#define FDCAN_IR_TCF              FDCAN_IR_TCF_Msk
#define FDCAN_IR_TCF_Msk          (0x1UL << FDCAN_IR_TCF_Pos)
#define FDCAN_IR_TCF_Pos          (10U)
#define FDCAN_IR_TC_Msk           (0x1UL << FDCAN_IR_TC_Pos)
#define FDCAN_IR_TC_Pos           (9U)
#define FDCAN_IR_TFE              FDCAN_IR_TFE_Msk
#define FDCAN_IR_TFE_Msk          (0x1UL << FDCAN_IR_TFE_Pos)
#define FDCAN_IR_TFE_Pos          (11U)
#define FDCAN_IR_WDI              FDCAN_IR_WDI_Msk
#define FDCAN_IR_WDI_Msk          (0x1UL << FDCAN_IR_WDI_Pos)
#define FDCAN_IR_WDI_Pos          (26U)
#define FDCAN_MODE_NORMAL               ((uint32_t)0x00000000U)
#define FDCAN_NO_TX_EVENTS    ((uint32_t)0x00000000U)
#define FDCAN_PSR_BO              FDCAN_PSR_BO_Msk
#define FDCAN_PSR_BO_Msk          (0x1UL << FDCAN_PSR_BO_Pos)
#define FDCAN_PSR_BO_Pos          (7U)
#define FDCAN_PSR_LEC             FDCAN_PSR_LEC_Msk
#define FDCAN_PSR_LEC_Msk         (0x7UL << FDCAN_PSR_LEC_Pos)
#define FDCAN_PSR_LEC_Pos         (0U)
#define FDCAN_REJECT             ((uint32_t)0x00000002U)
#define FDCAN_REJECT_REMOTE ((uint32_t)0x00000001U)
#define FDCAN_RXF0C_F0OM          FDCAN_RXF0C_F0OM_Msk
#define FDCAN_RXF0C_F0OM_Msk      (0x1UL << FDCAN_RXF0C_F0OM_Pos)
#define FDCAN_RXF0C_F0OM_Pos      (31U)
#define FDCAN_RXF0S_F0GI          FDCAN_RXF0S_F0GI_Msk
#define FDCAN_RXF0S_F0GI_Msk      (0x3FUL << FDCAN_RXF0S_F0GI_Pos)
#define FDCAN_RXF0S_F0GI_Pos      (8U)
#define FDCAN_RX_FIFO0    ((uint32_t)0x00000040U)
#define FDCAN_RX_FIFO_BLOCKING  ((uint32_t)0x00000000U)
#define FDCAN_STANDARD_ID ((uint32_t)0x00000000U)
#endif
