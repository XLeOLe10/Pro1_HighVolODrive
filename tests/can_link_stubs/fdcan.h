#ifndef CAN_LINK_HOST_FDCAN_H
#define CAN_LINK_HOST_FDCAN_H
/* HOST-only register/HAL seam. Relevant constants below are copied verbatim
 * from the project's STM32H750 CMSIS/HAL headers, not a CAN bus simulator. */
#include <stdint.h>
#define DISABLE 0U
#define HSE_VALUE 25000000U
#define RCC_CR_HSERDY_Pos (17U)
#define RCC_CR_HSERDY_Msk (0x1UL << RCC_CR_HSERDY_Pos)
#define RCC_CR_HSERDY RCC_CR_HSERDY_Msk
#define HAL_FDCAN_STATE_READY 1U
#define HAL_FDCAN_STATE_BUSY 2U
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct {
 volatile uint32_t IR, PSR, ECR, RXF0S, RXF0C, RXF0A, IE, ILE;
 volatile uint32_t NBTP, TSCC, TSCV, TOCC, TOCV, CCCR, RXESC;
} FDCAN_GlobalTypeDef;
typedef struct { volatile uint32_t APB1HFZ1; } HostDebugRegisters;
typedef struct { volatile uint32_t D2CCIP1R, CR; } HostRccRegisters;
typedef struct { volatile uint32_t CCFG; } HostCanCcuRegisters;
extern HostDebugRegisters host_debug_registers;
extern HostRccRegisters host_rcc_registers;
extern HostCanCcuRegisters host_ccu_registers;
#define DBGMCU (&host_debug_registers)
#define RCC (&host_rcc_registers)
#define FDCAN_CCU (&host_ccu_registers)
extern FDCAN_GlobalTypeDef host_can_registers;
extern uint32_t host_message_ram[161];
#define FDCAN1 (&host_can_registers)
#define SRAMCAN_BASE ((uintptr_t)host_message_ram)
typedef struct {
 uint32_t FrameFormat, Mode, AutoRetransmission, MessageRAMOffset;
 uint32_t StdFiltersNbr, ExtFiltersNbr, RxFifo0ElmtsNbr, RxFifo0ElmtSize;
 uint32_t RxFifo1ElmtsNbr, RxBuffersNbr, TxEventsNbr, TxBuffersNbr;
 uint32_t TxFifoQueueElmtsNbr, TxElmtSize;
 uint32_t NominalPrescaler, NominalSyncJumpWidth, NominalTimeSeg1, NominalTimeSeg2;
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
typedef struct { uint32_t Identifier, IdType, RxFrameType, FDFormat, BitRateSwitch, DataLength, RxTimestamp; } FDCAN_RxHeaderTypeDef;
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
/* Timebase HAL seam: mirrors real register effects, including TOCV write-to-
 * reload behavior. It is not a simulated CAN bit clock or interrupt scheduler. */
HAL_StatusTypeDef HAL_FDCAN_ConfigTimestampCounter(FDCAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_EnableTimestampCounter(FDCAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_ConfigTimeoutCounter(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_EnableTimeoutCounter(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ResetTimestampCounter(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ResetTimeoutCounter(FDCAN_HandleTypeDef *);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t);
#define __HAL_RCC_GET_FDCAN_SOURCE() (RCC->D2CCIP1R & RCC_D2CCIP1R_FDCANSEL)
#define __HAL_DBGMCU_UnFreeze_FDCAN() (DBGMCU->APB1HFZ1 &= ~DBGMCU_APB1HFZ1_DBG_FDCAN)
#define FDCAN_CCCR_INIT_Pos       (0U)
#define FDCAN_CCCR_INIT_Msk       (0x1UL << FDCAN_CCCR_INIT_Pos)               /*!< 0x00000001 */
#define FDCAN_CCCR_INIT           FDCAN_CCCR_INIT_Msk                          /*!<Initialization                           */
#define FDCAN_CCCR_CCE_Pos        (1U)
#define FDCAN_CCCR_CCE_Msk        (0x1UL << FDCAN_CCCR_CCE_Pos)                /*!< 0x00000002 */
#define FDCAN_CCCR_CCE            FDCAN_CCCR_CCE_Msk                           /*!<Configuration Change Enable              */
#define FDCAN_CCCR_ASM_Pos        (2U)
#define FDCAN_CCCR_ASM_Msk        (0x1UL << FDCAN_CCCR_ASM_Pos)                /*!< 0x00000004 */
#define FDCAN_CCCR_ASM            FDCAN_CCCR_ASM_Msk                           /*!<ASM Restricted Operation Mode            */
#define FDCAN_CCCR_CSA_Pos        (3U)
#define FDCAN_CCCR_CSA_Msk        (0x1UL << FDCAN_CCCR_CSA_Pos)                /*!< 0x00000008 */
#define FDCAN_CCCR_CSA            FDCAN_CCCR_CSA_Msk                           /*!<Clock Stop Acknowledge                   */
#define FDCAN_CCCR_CSR_Pos        (4U)
#define FDCAN_CCCR_CSR_Msk        (0x1UL << FDCAN_CCCR_CSR_Pos)                /*!< 0x00000010 */
#define FDCAN_CCCR_CSR            FDCAN_CCCR_CSR_Msk                           /*!<Clock Stop Request                       */
#define FDCAN_CCCR_MON_Pos        (5U)
#define FDCAN_CCCR_MON_Msk        (0x1UL << FDCAN_CCCR_MON_Pos)                /*!< 0x00000020 */
#define FDCAN_CCCR_MON            FDCAN_CCCR_MON_Msk                           /*!<Bus Monitoring Mode                      */
#define FDCAN_CCCR_DAR_Pos        (6U)
#define FDCAN_CCCR_DAR_Msk        (0x1UL << FDCAN_CCCR_DAR_Pos)                /*!< 0x00000040 */
#define FDCAN_CCCR_DAR            FDCAN_CCCR_DAR_Msk                           /*!<Disable Automatic Retransmission         */
#define FDCAN_CCCR_TEST_Pos       (7U)
#define FDCAN_CCCR_TEST_Msk       (0x1UL << FDCAN_CCCR_TEST_Pos)               /*!< 0x00000080 */
#define FDCAN_CCCR_TEST           FDCAN_CCCR_TEST_Msk                          /*!<Test Mode Enable                         */
#define FDCAN_CCCR_FDOE_Pos       (8U)
#define FDCAN_CCCR_FDOE_Msk       (0x1UL << FDCAN_CCCR_FDOE_Pos)               /*!< 0x00000100 */
#define FDCAN_CCCR_FDOE           FDCAN_CCCR_FDOE_Msk                          /*!<FD Operation Enable                      */
#define FDCAN_CCCR_BRSE_Pos       (9U)
#define FDCAN_CCCR_BRSE_Msk       (0x1UL << FDCAN_CCCR_BRSE_Pos)               /*!< 0x00000200 */
#define FDCAN_CCCR_BRSE           FDCAN_CCCR_BRSE_Msk                          /*!<FDCAN Bit Rate Switching                 */
#define FDCAN_CCCR_PXHD_Pos       (12U)
#define FDCAN_CCCR_PXHD_Msk       (0x1UL << FDCAN_CCCR_PXHD_Pos)               /*!< 0x00001000 */
#define FDCAN_CCCR_PXHD           FDCAN_CCCR_PXHD_Msk                          /*!<Protocol Exception Handling Disable      */
#define FDCAN_CCCR_EFBI_Pos       (13U)
#define FDCAN_CCCR_EFBI_Msk       (0x1UL << FDCAN_CCCR_EFBI_Pos)               /*!< 0x00002000 */
#define FDCAN_CCCR_EFBI           FDCAN_CCCR_EFBI_Msk                          /*!<Edge Filtering during Bus Integration    */
#define FDCAN_CCCR_TXP_Pos        (14U)
#define FDCAN_CCCR_TXP_Msk        (0x1UL << FDCAN_CCCR_TXP_Pos)                /*!< 0x00004000 */
#define FDCAN_CCCR_TXP            FDCAN_CCCR_TXP_Msk                           /*!<Two CAN bit times Pause                  */
#define FDCAN_CCCR_NISO_Pos       (15U)
#define FDCAN_CCCR_NISO_Msk       (0x1UL << FDCAN_CCCR_NISO_Pos)               /*!< 0x00008000 */
#define FDCAN_CCCR_NISO           FDCAN_CCCR_NISO_Msk                          /*!<Non ISO Operation                        */
#define FDCAN_NBTP_NTSEG2_Pos     (0U)
#define FDCAN_NBTP_NTSEG2_Msk     (0x7FUL << FDCAN_NBTP_NTSEG2_Pos)            /*!< 0x0000007F */
#define FDCAN_NBTP_NTSEG2         FDCAN_NBTP_NTSEG2_Msk                        /*!<Nominal Time segment after sample point  */
#define FDCAN_NBTP_NTSEG1_Pos     (8U)
#define FDCAN_NBTP_NTSEG1_Msk     (0xFFUL << FDCAN_NBTP_NTSEG1_Pos)            /*!< 0x0000FF00 */
#define FDCAN_NBTP_NTSEG1         FDCAN_NBTP_NTSEG1_Msk                        /*!<Nominal Time segment before sample point */
#define FDCAN_NBTP_NBRP_Pos       (16U)
#define FDCAN_NBTP_NBRP_Msk       (0x1FFUL << FDCAN_NBTP_NBRP_Pos)             /*!< 0x01FF0000 */
#define FDCAN_NBTP_NBRP           FDCAN_NBTP_NBRP_Msk                          /*!<Bit Rate Prescaler                       */
#define FDCAN_NBTP_NSJW_Pos       (25U)
#define FDCAN_NBTP_NSJW_Msk       (0x7FUL << FDCAN_NBTP_NSJW_Pos)              /*!< 0xFE000000 */
#define FDCAN_NBTP_NSJW           FDCAN_NBTP_NSJW_Msk                          /*!<Nominal (Re)Synchronization Jump Width   */
#define FDCAN_TSCC_TSS_Pos        (0U)
#define FDCAN_TSCC_TSS_Msk        (0x3UL << FDCAN_TSCC_TSS_Pos)                /*!< 0x00000003 */
#define FDCAN_TSCC_TSS            FDCAN_TSCC_TSS_Msk                           /*!<Timestamp Select                         */
#define FDCAN_TSCC_TCP_Pos        (16U)
#define FDCAN_TSCC_TCP_Msk        (0xFUL << FDCAN_TSCC_TCP_Pos)                /*!< 0x000F0000 */
#define FDCAN_TSCC_TCP            FDCAN_TSCC_TCP_Msk                           /*!<Timestamp Counter Prescaler              */
#define FDCAN_TSCV_TSC_Pos        (0U)
#define FDCAN_TSCV_TSC_Msk        (0xFFFFUL << FDCAN_TSCV_TSC_Pos)             /*!< 0x0000FFFF */
#define FDCAN_TSCV_TSC            FDCAN_TSCV_TSC_Msk                           /*!<Timestamp Counter                        */
#define FDCAN_TOCC_ETOC_Pos       (0U)
#define FDCAN_TOCC_ETOC_Msk       (0x1UL << FDCAN_TOCC_ETOC_Pos)               /*!< 0x00000001 */
#define FDCAN_TOCC_ETOC           FDCAN_TOCC_ETOC_Msk                          /*!<Enable Timeout Counter                   */
#define FDCAN_TOCC_TOS_Pos        (1U)
#define FDCAN_TOCC_TOS_Msk        (0x3UL << FDCAN_TOCC_TOS_Pos)                /*!< 0x00000006 */
#define FDCAN_TOCC_TOS            FDCAN_TOCC_TOS_Msk                           /*!<Timeout Select                           */
#define FDCAN_TOCC_TOP_Pos        (16U)
#define FDCAN_TOCC_TOP_Msk        (0xFFFFUL << FDCAN_TOCC_TOP_Pos)             /*!< 0xFFFF0000 */
#define FDCAN_TOCC_TOP            FDCAN_TOCC_TOP_Msk                           /*!<Timeout Period                           */
#define FDCAN_TOCV_TOC_Pos        (0U)
#define FDCAN_TOCV_TOC_Msk        (0xFFFFUL << FDCAN_TOCV_TOC_Pos)             /*!< 0x0000FFFF */
#define FDCAN_TOCV_TOC            FDCAN_TOCV_TOC_Msk                           /*!<Timeout Counter                          */
#define FDCAN_IR_TSW_Pos          (16U)
#define FDCAN_IR_TSW_Msk          (0x1UL << FDCAN_IR_TSW_Pos)                  /*!< 0x00010000 */
#define FDCAN_IR_TSW              FDCAN_IR_TSW_Msk                             /*!<Timestamp Wraparound                     */
#define FDCAN_IR_TOO_Pos          (18U)
#define FDCAN_IR_TOO_Msk          (0x1UL << FDCAN_IR_TOO_Pos)                  /*!< 0x00040000 */
#define FDCAN_IR_TOO              FDCAN_IR_TOO_Msk                             /*!<Timeout Occurred                         */
#define FDCANCCU_CCFG_TQBT_Pos       (0U)
#define FDCANCCU_CCFG_TQBT_Msk       (0x1FUL << FDCANCCU_CCFG_TQBT_Pos)        /*!< 0x0000001F */
#define FDCANCCU_CCFG_TQBT           FDCANCCU_CCFG_TQBT_Msk                    /*!<Time Quanta per Bit Time                */
#define FDCANCCU_CCFG_BCC_Pos        (6U)
#define FDCANCCU_CCFG_BCC_Msk        (0x1UL << FDCANCCU_CCFG_BCC_Pos)          /*!< 0x00000040 */
#define FDCANCCU_CCFG_BCC            FDCANCCU_CCFG_BCC_Msk                     /*!<Bypass Clock Calibration                */
#define FDCANCCU_CCFG_CFL_Pos        (7U)
#define FDCANCCU_CCFG_CFL_Msk        (0x1UL << FDCANCCU_CCFG_CFL_Pos)          /*!< 0x00000080 */
#define FDCANCCU_CCFG_CFL            FDCANCCU_CCFG_CFL_Msk                     /*!<Calibration Field Length                */
#define FDCANCCU_CCFG_OCPM_Pos       (8U)
#define FDCANCCU_CCFG_OCPM_Msk       (0xFFUL << FDCANCCU_CCFG_OCPM_Pos)        /*!< 0x0000FF00 */
#define FDCANCCU_CCFG_OCPM           FDCANCCU_CCFG_OCPM_Msk                    /*!<Oscillator Clock Periods Minimum        */
#define FDCANCCU_CCFG_CDIV_Pos       (16U)
#define FDCANCCU_CCFG_CDIV_Msk       (0xFUL << FDCANCCU_CCFG_CDIV_Pos)         /*!< 0x000F0000 */
#define FDCANCCU_CCFG_CDIV           FDCANCCU_CCFG_CDIV_Msk                    /*!<Clock Divider                           */
#define FDCANCCU_CCFG_SWR_Pos        (31U)
#define FDCANCCU_CCFG_SWR_Msk        (0x1UL << FDCANCCU_CCFG_SWR_Pos)          /*!< 0x80000000 */
#define FDCANCCU_CCFG_SWR            FDCANCCU_CCFG_SWR_Msk                     /*!<Software Reset                          */
#define RCC_D2CCIP1R_FDCANSEL_Pos              (28U)
#define RCC_D2CCIP1R_FDCANSEL_Msk              (0x3UL << RCC_D2CCIP1R_FDCANSEL_Pos) /*!< 0x30000000 */
#define RCC_D2CCIP1R_FDCANSEL                  RCC_D2CCIP1R_FDCANSEL_Msk
#define RCC_D2CCIP1R_FDCANSEL_0                (0x1UL << RCC_D2CCIP1R_FDCANSEL_Pos) /*!< 0x10000000 */
#define RCC_D2CCIP1R_FDCANSEL_1                (0x2UL << RCC_D2CCIP1R_FDCANSEL_Pos) /*!< 0x20000000 */
#define DBGMCU_APB1HFZ1_DBG_FDCAN_Pos     (8U)
#define DBGMCU_APB1HFZ1_DBG_FDCAN_Msk     (0x1UL << DBGMCU_APB1HFZ1_DBG_FDCAN_Pos) /*!< 0x00000100 */
#define DBGMCU_APB1HFZ1_DBG_FDCAN         DBGMCU_APB1HFZ1_DBG_FDCAN_Msk
#define FDCAN_TIMESTAMP_INTERNAL ((uint32_t)0x00000001U) /*!< Timestamp counter value incremented according to TCP */
#define FDCAN_TIMESTAMP_PRESC_16 ((uint32_t)0x000F0000U) /*!< Timestamp counter time unit in equal to CAN bit time multiplied by 16 */
#define FDCAN_TIMEOUT_CONTINUOUS    ((uint32_t)0x00000000U) /*!< Timeout continuous operation        */
#define RCC_PERIPHCLK_FDCAN            ((uint64_t)(0x00008000U))
#define RCC_FDCANCLKSOURCE_HSE         (0x00000000U)
#define FDCAN_RXF0C_F0SA_Pos      (2U)
#define FDCAN_RXF0C_F0SA_Msk      (0x3FFFUL << FDCAN_RXF0C_F0SA_Pos)           /*!< 0x0000FFFC */
#define FDCAN_RXF0C_F0SA          FDCAN_RXF0C_F0SA_Msk                         /*!<Rx FIFO 0 Start Address                   */
#define FDCAN_RXF0C_F0S_Pos       (16U)
#define FDCAN_RXF0C_F0S_Msk       (0x7FUL << FDCAN_RXF0C_F0S_Pos)              /*!< 0x007F0000 */
#define FDCAN_RXF0C_F0S           FDCAN_RXF0C_F0S_Msk                          /*!<Number of Rx FIFO 0 elements              */
#define FDCAN_RXF0C_F0WM_Pos      (24U)
#define FDCAN_RXF0C_F0WM_Msk      (0x7FUL << FDCAN_RXF0C_F0WM_Pos)             /*!< 0x7F000000 */
#define FDCAN_RXF0C_F0WM          FDCAN_RXF0C_F0WM_Msk                         /*!<FIFO 0 Watermark                          */
#define FDCAN_RXESC_F0DS_Pos      (0U)
#define FDCAN_RXESC_F0DS_Msk      (0x7UL << FDCAN_RXESC_F0DS_Pos)              /*!< 0x00000007 */
#define FDCAN_RXESC_F0DS          FDCAN_RXESC_F0DS_Msk                         /*!<Rx FIFO 1 Data Field Size                 */
#define FDCAN_RXF0S_F0FL 0x0000007FU
#endif
