#include "ad2s1210.h"
#include "app_policy.h"
#include "main.h"
#include "spi.h"

#define AD2S1210_REG_EXC_FREQ              0x91U
#define AD2S1210_REG_CONTROL               0x92U
#define AD2S1210_REG_DOS_OVERRANGE         0x89U
#define AD2S1210_REG_DOS_MISMATCH          0x8AU
#define AD2S1210_REG_LOT_HIGH              0x8DU
#define AD2S1210_REG_LOT_LOW               0x8EU
#define AD2S1210_REG_SOFT_RESET            0xF0U
#define AD2S1210_REG_FAULT                 0xFFU
#define AD2S1210_CONTROL_16BIT             0x7FU
#define AD2S1210_EXC_FCW_8MHZ              0x28U

/* Relaxed low-voltage commissioning thresholds, all within the Rev. B data
   sheet ranges.  These affect D5/D4/D3 only; D7 input clipping stays active. */
#define AD2S1210_DOS_OVERRANGE_RELAXED     0x78U /* 4.56 V, 38 mV/LSB */
#define AD2S1210_DOS_MISMATCH_RELAXED      0x14U /* 0.76 V, 38 mV/LSB */
#define AD2S1210_LOT_HIGH_RELAXED          0x38U /* 5.04 deg at 16 bit */
#define AD2S1210_LOT_LOW_RELAXED           0x0BU /* 0.99 deg at 16 bit */

#define AD2S1210_CLKIN_HZ                  8000000UL
#define AD2S1210_TWO_PI                    6.28318530718f
#define AD2S1210_POSITION_SCALE_RAD        (AD2S1210_TWO_PI / 65536.0f)
#define AD2S1210_VEL_MAX_RPS_16BIT         122.0703125f
#define AD2S1210_RESOLVER_POLE_PAIRS       4.0f
#define AD2S1210_SPEED_FILTER_ALPHA        0.10f
#define AD2S1210_SLOW_LINK_DIAG             0U
#define AD2S1210_VALID_STREAK_REQUIRED       100U
#define AD2S1210_VELOCITY_DEADBAND_COUNTS      2
#define AD2S1210_ACQUISITION_BUDGET_US       800U

#define AD2S1210_WR_PORT                   GPIOE
#define AD2S1210_WR_PIN                    GPIO_PIN_1
#define AD2S1210_A0_PORT                   GPIOE
#define AD2S1210_A0_PIN                    GPIO_PIN_0
#define AD2S1210_A1_PORT                   GPIOB
#define AD2S1210_A1_PIN                    GPIO_PIN_9
#define AD2S1210_SAMPLE_PORT               GPIOB
#define AD2S1210_SAMPLE_PIN                GPIO_PIN_8

volatile uint16_t ad2s_position_raw = 0U;
volatile int16_t ad2s_velocity_raw = 0;
volatile float ad2s_position_rad = 0.0f;
volatile float ad2s_speed_rpm = 0.0f;
volatile float ad2s_speed_rpm_filt = 0.0f;
volatile uint8_t ad2s_fault = 0xFFU;
volatile uint8_t ad2s_fault_initial = 0xFFU;
volatile uint8_t ad2s_fault_after_reset = 0xFFU;
volatile uint8_t ad2s_fault_first_direct = 0xFFU;
volatile uint8_t ad2s_fault_config_confirm = 0xFFU;
volatile uint8_t ad2s_fault_first_hal = 0xFFU;
volatile uint8_t ad2s_fault_recovery_attempted = 0U;
volatile uint8_t ad2s_fault_before_recovery = 0xFFU;
volatile uint8_t ad2s_fault_after_recovery = 0xFFU;
volatile uint8_t ad2s_fault_recovery_status = (uint8_t)HAL_ERROR;
volatile uint8_t ad2s_fault_diagnose_status = (uint8_t)HAL_ERROR;
volatile uint8_t ad2s_slow_link_diag_active = AD2S1210_SLOW_LINK_DIAG;
volatile uint32_t ad2s_spi_kernel_hz = 64000000UL;
volatile uint32_t ad2s_spi_sclk_hz = 250000UL;
volatile uint8_t ad2s_runtime_spi_status = (uint8_t)HAL_ERROR;
volatile uint32_t ad2s_spi_cfg1_readback = 0U;
volatile uint8_t ad2s_software_reset_status = (uint8_t)HAL_ERROR;
volatile uint8_t ad2s_init_status = (uint8_t)HAL_ERROR;
volatile uint8_t ad2s_config_parity_error_count = 0U;
volatile uint8_t ad2s_config_error_address = 0U;
volatile uint8_t ad2s_config_error_value = 0U;
volatile uint8_t ad2s_config_repair_count = 0U;
volatile uint8_t ad2s_config_repair_failed_count = 0U;
volatile uint8_t ad2s_config_repair_error_address = 0U;
volatile uint8_t ad2s_config_repair_error_value = 0U;
volatile uint8_t ad2s_control_before = 0U;
volatile uint8_t ad2s_control_readback = 0U;
volatile uint8_t ad2s_exc_readback = 0U;
volatile uint8_t ad2s_dos_overrange_readback = 0xFFU;
volatile uint8_t ad2s_dos_mismatch_readback = 0xFFU;
volatile uint8_t ad2s_lot_high_readback = 0xFFU;
volatile uint8_t ad2s_lot_low_readback = 0xFFU;
volatile uint8_t ad2s_threshold_config_status = (uint8_t)HAL_ERROR;
volatile uint32_t ad2s_exc_frequency_hz = 0U;
volatile uint8_t ad2s_initialized = 0U;
volatile uint8_t ad2s_feedback_valid = 0U;
volatile uint8_t ad2s_position_data_valid = 0U;
volatile uint8_t ad2s_fault_latched = 0U;
volatile uint16_t ad2s_valid_streak = 0U;
volatile uint32_t ad2s_position_read_count = 0U;
volatile uint32_t ad2s_velocity_read_count = 0U;
volatile uint32_t ad2s_spi_error_count = 0U;
volatile uint8_t ad2s_dwt_ready = 0U;
volatile uint32_t ad2s_acquisition_last_cycles = 0U;
volatile uint32_t ad2s_acquisition_max_cycles = 0U;
volatile uint32_t ad2s_acquisition_budget_cycles = 0U;
volatile uint32_t ad2s_acquisition_over_budget_count = 0U;

volatile uint32_t ad2s_snapshot_max_cycles = 0U;
volatile uint32_t ad2s_sample_edge_max_cycles = 0U;
volatile uint32_t ad2s_sample_interval_cycles = 0U;
volatile uint32_t ad2s_sample_interval_max_cycles = 0U;
volatile uint32_t ad2s_sample_gap_count = 0U;

static uint16_t ad2s_previous_position_raw = 0U;
static uint32_t ad2s_previous_sample_cycles = 0U;
static uint32_t ad2s_previous_sample_tick_ms = 0U;
static uint8_t ad2s_position_speed_ready = 0U;
static volatile AD2S1210_FeedbackSnapshot ad2s_snapshot;

#if AXIS_COMMAND_BENCH_ENABLE
/* AD2S AGE TRACE BEGIN: actual event pairing tested on the host. */
volatile AD2S1210_AgeTrace ad2s_age_trace = {
    AD2S_AGE_IDLE, 0U, 0U, UINT32_MAX, UINT32_MAX, UINT32_MAX
};
static volatile uint32_t age_source_cycles;
static volatile uint32_t age_source_tick_ms;

void AD2S1210_ArmAgeTrace(const AD2S1210_FeedbackSnapshot *source,
                         uint32_t check_cycles)
{
    uint32_t state = AD2S_AGE_UNKNOWN;
    if (ad2s_age_trace.state != AD2S_AGE_IDLE)
        return;

    if (source != NULL)
    {
        age_source_cycles = source->sample_cycles;
        age_source_tick_ms = source->sample_tick_ms;
        ad2s_age_trace.source_sequence = source->sequence;
        ad2s_age_trace.check_age_cycles = (uint32_t)(check_cycles - source->sample_cycles);
        if ((source->valid != 0U) && (source->speed_valid != 0U) &&
            (ad2s_dwt_ready != 0U))
            state = AD2S_AGE_PENDING;
    }
    __DMB();
    ad2s_age_trace.state = state;
}

/* Called only by the serialized publisher after observing PENDING. On a
   single core TIM1 may have armed *after* this very snapshot became visible.
   Its matching source callback then leaves the request for the next attempt. */
static void AD2S1210_CompleteAgeTrace(const AD2S1210_FeedbackSnapshot *next,
                                      uint32_t published_cycles)
{
    uint32_t source_seq = ad2s_age_trace.source_sequence;
    uint32_t state = AD2S_AGE_UNKNOWN;
    uint32_t edge_span;
    uint32_t publish_span;
    uint32_t wall_ms;

    if (next->sequence == source_seq)
        return;
    ad2s_age_trace.next_sequence = next->sequence;
    if (next->sequence == (uint32_t)(source_seq + 1U))
    {
        if (next->valid == 0U)
        {
            /* The invalid snapshot carries the old timestamp. */
            state = AD2S_AGE_INVALID_NEXT;
        }
        else
        {
            edge_span = (uint32_t)(next->sample_cycles - age_source_cycles);
            publish_span = (uint32_t)(published_cycles - age_source_cycles);
            wall_ms = (uint32_t)(HAL_GetTick() - age_source_tick_ms);
            /* 1 second is far below half a DWT wrap at 200 MHz. The separate
               millisecond clock rejects a long pause across a DWT wrap.
               A replay captured while clocks are halted needs a fresh run. */
            if ((ad2s_dwt_ready != 0U) &&
                ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) &&
                (wall_ms <= 1000U) &&
                ((uint32_t)(next->sample_tick_ms - age_source_tick_ms) <= wall_ms) &&
                (publish_span < 0x80000000UL) && (publish_span <= SystemCoreClock) &&
                (ad2s_age_trace.check_age_cycles != 0U) &&
                (ad2s_age_trace.check_age_cycles < publish_span) &&
                (edge_span != 0U) && (edge_span < publish_span))
            {
                ad2s_age_trace.edge_interval_cycles = edge_span;
                ad2s_age_trace.edge_to_publish_cycles = publish_span - edge_span;
                state = (edge_span <= ad2s_age_trace.check_age_cycles) ?
                    AD2S_AGE_EDGE_BY_CHECK : AD2S_AGE_EDGE_AFTER_CHECK;
            }
        }
    }
    __DMB();
    ad2s_age_trace.state = state;
}
/* AD2S AGE TRACE END */
#endif

static void AD2S1210_PublishSnapshot(const AD2S1210_FeedbackSnapshot *snapshot)
{
    uint32_t saved_primask;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
#if AXIS_COMMAND_BENCH_ENABLE
    uint32_t published_cycles;
#endif

    /* PRIMASK also excludes priority-0 TIM1. This region contains only
       eight fixed field copies/barriers, no SPI or float calculations.
       NMI/fault handlers must not access this frame. */
    saved_primask = __get_PRIMASK();
    __disable_irq();
    start_cycles = DWT->CYCCNT;
    __DMB();
    ad2s_snapshot.position_rad = snapshot->position_rad;
    ad2s_snapshot.speed_rpm = snapshot->speed_rpm;
    ad2s_snapshot.sample_cycles = snapshot->sample_cycles;
    ad2s_snapshot.sample_tick_ms = snapshot->sample_tick_ms;
    ad2s_snapshot.sequence = snapshot->sequence;
    ad2s_snapshot.valid = snapshot->valid;
    ad2s_snapshot.speed_valid = snapshot->speed_valid;
    ad2s_snapshot.fault = snapshot->fault;
    __DMB();
#if AXIS_COMMAND_BENCH_ENABLE
    /* Reuse the existing endpoint after the payload barrier, before unmask. */
    published_cycles = DWT->CYCCNT;
    elapsed_cycles = (uint32_t)(published_cycles - start_cycles);
#else
    elapsed_cycles = (uint32_t)(DWT->CYCCNT - start_cycles);
#endif
    if (elapsed_cycles > ad2s_snapshot_max_cycles)
    {
        ad2s_snapshot_max_cycles = elapsed_cycles;
    }
    __set_PRIMASK(saved_primask);
#if AXIS_COMMAND_BENCH_ENABLE
    if (ad2s_age_trace.state == AD2S_AGE_PENDING)
        AD2S1210_CompleteAgeTrace(snapshot, published_cycles);
#endif
}

void AD2S1210_GetSnapshot(AD2S1210_FeedbackSnapshot *snapshot)
{
    uint32_t saved_primask;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;

    if (snapshot == NULL)
    {
        return;
    }
    /* PRIMASK also excludes priority-0 TIM1. This region contains only
       eight fixed field copies/barriers, no SPI or float calculations.
       NMI/fault handlers must not access this frame. */
    saved_primask = __get_PRIMASK();
    __disable_irq();
    start_cycles = DWT->CYCCNT;
    __DMB();
    snapshot->position_rad = ad2s_snapshot.position_rad;
    snapshot->speed_rpm = ad2s_snapshot.speed_rpm;
    snapshot->sample_cycles = ad2s_snapshot.sample_cycles;
    snapshot->sample_tick_ms = ad2s_snapshot.sample_tick_ms;
    snapshot->sequence = ad2s_snapshot.sequence;
    snapshot->valid = ad2s_snapshot.valid;
    snapshot->speed_valid = ad2s_snapshot.speed_valid;
    snapshot->fault = ad2s_snapshot.fault;
    __DMB();
    elapsed_cycles = (uint32_t)(DWT->CYCCNT - start_cycles);
    if (elapsed_cycles > ad2s_snapshot_max_cycles)
    {
        ad2s_snapshot_max_cycles = elapsed_cycles;
    }
    __set_PRIMASK(saved_primask);
}

static float AD2S1210_PositionDeltaToMechanicalRPM(
    uint16_t position_raw, uint32_t sample_cycles, uint32_t sample_tick_ms,
    uint32_t *speed_valid)
{
    int16_t delta;
    uint32_t elapsed_cycles;
    uint32_t elapsed_ms;

    *speed_valid = 0U;
    if (ad2s_position_speed_ready == 0U)
    {
        ad2s_previous_position_raw = position_raw;
        ad2s_previous_sample_cycles = sample_cycles;
        ad2s_previous_sample_tick_ms = sample_tick_ms;
        ad2s_position_speed_ready = 1U;
        return 0.0f;
    }

    elapsed_cycles = (uint32_t)(sample_cycles - ad2s_previous_sample_cycles);
    elapsed_ms = (uint32_t)(sample_tick_ms - ad2s_previous_sample_tick_ms);
    delta = (int16_t)(position_raw - ad2s_previous_position_raw);
    ad2s_previous_position_raw = position_raw;
    ad2s_previous_sample_cycles = sample_cycles;
    ad2s_previous_sample_tick_ms = sample_tick_ms;
    ad2s_sample_interval_cycles = elapsed_cycles;
    if (elapsed_cycles > ad2s_sample_interval_max_cycles)
    {
        ad2s_sample_interval_max_cycles = elapsed_cycles;
    }

    /* Millisecond time rejects gaps longer than a DWT wrap too. An overdue
       frame resets the difference baseline, never inventing a 1 ms delta.
       This is a scheduling budget, not a motor-speed aliasing guarantee. */
    if ((ad2s_dwt_ready == 0U) || (elapsed_cycles == 0U) ||
        (elapsed_ms > APP_FEEDBACK_MAX_AGE_MS) ||
        (elapsed_cycles > ((SystemCoreClock / 1000U) *
                           APP_FEEDBACK_MAX_AGE_MS)))
    {
        ad2s_sample_gap_count++;
        return 0.0f;
    }

    /* Position change must be below half a resolver revolution between
       samples. Signed subtraction then handles crossing 0/65535. */
    if ((delta >= -AD2S1210_VELOCITY_DEADBAND_COUNTS) &&
        (delta <= AD2S1210_VELOCITY_DEADBAND_COUNTS))
    {
        delta = 0;
    }
    *speed_valid = 1U;
    return (float)delta *
           (60.0f / 65536.0f / AD2S1210_RESOLVER_POLE_PAIRS) *
           ((float)SystemCoreClock / (float)elapsed_cycles);
}

static void AD2S1210_DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    ad2s_dwt_ready =
        ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;
}

static void AD2S1210_DelayUs(uint32_t us)
{
    uint32_t start;
    uint32_t ticks_per_us;
    uint32_t ticks;

    if (us == 0U)
    {
        return;
    }

    if (ad2s_dwt_ready == 0U)
    {
        volatile uint32_t fallback =
            us * ((SystemCoreClock / 1000000U) / 8U + 1U);
        while (fallback-- > 0U)
        {
            __NOP();
        }
        return;
    }

    ticks_per_us = SystemCoreClock / 1000000U;
    if (ticks_per_us == 0U)
    {
        ticks_per_us = 1U;
    }
    ticks = ticks_per_us * us;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks)
    {
        __NOP();
    }
}

static void AD2S1210_ShortDelay(void)
{
    AD2S1210_DelayUs(1U);
}

static void AD2S1210_ModePosition(void)
{
    HAL_GPIO_WritePin(AD2S1210_A0_PORT, AD2S1210_A0_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AD2S1210_A1_PORT, AD2S1210_A1_PIN, GPIO_PIN_RESET);
}

static void AD2S1210_ModeVelocity(void)
{
    HAL_GPIO_WritePin(AD2S1210_A0_PORT, AD2S1210_A0_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AD2S1210_A1_PORT, AD2S1210_A1_PIN, GPIO_PIN_SET);
}

static void AD2S1210_ModeConfig(void)
{
    HAL_GPIO_WritePin(AD2S1210_A0_PORT, AD2S1210_A0_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(AD2S1210_A1_PORT, AD2S1210_A1_PIN, GPIO_PIN_SET);
}

static void AD2S1210_PulseSample(uint32_t *sample_cycles,
                                    uint32_t *sample_tick_ms)
{
    uint32_t saved_primask;
    uint32_t edge_cycles;
    uint32_t edge_tick_ms;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;

    HAL_GPIO_WritePin(AD2S1210_SAMPLE_PORT, AD2S1210_SAMPLE_PIN, GPIO_PIN_SET);
    AD2S1210_ShortDelay();
    /* Do not allow preemption between the sample edge and its timestamp.
       Neither the following 1 us hold nor the SPI transfer is masked. */
    saved_primask = __get_PRIMASK();
    __disable_irq();
    start_cycles = DWT->CYCCNT;
    AD2S1210_SAMPLE_PORT->BSRR = (uint32_t)AD2S1210_SAMPLE_PIN << 16U;
    __DSB();
    edge_cycles = DWT->CYCCNT;
    edge_tick_ms = HAL_GetTick();
    __DMB();
    elapsed_cycles = (uint32_t)(DWT->CYCCNT - start_cycles);
    if (elapsed_cycles > ad2s_sample_edge_max_cycles)
    {
        ad2s_sample_edge_max_cycles = elapsed_cycles;
    }
    __set_PRIMASK(saved_primask);
    if (sample_cycles != NULL)
    {
        *sample_cycles = edge_cycles;
    }
    if (sample_tick_ms != NULL)
    {
        *sample_tick_ms = edge_tick_ms;
    }
    AD2S1210_ShortDelay();
    HAL_GPIO_WritePin(AD2S1210_SAMPLE_PORT, AD2S1210_SAMPLE_PIN, GPIO_PIN_SET);
    AD2S1210_ShortDelay();
}

static void AD2S1210_UpdateFeedbackHealth(uint8_t fault)
{
    ad2s_fault = fault;
    ad2s_fault_latched |= fault;

    if (fault != 0U)
    {
        ad2s_valid_streak = 0U;
        ad2s_feedback_valid = 0U;
        return;
    }

    if (ad2s_valid_streak < AD2S1210_VALID_STREAK_REQUIRED)
    {
        ad2s_valid_streak++;
    }
    if (ad2s_valid_streak >= AD2S1210_VALID_STREAK_REQUIRED)
    {
        ad2s_feedback_valid = 1U;
    }
}

static void AD2S1210_RecordAcquisitionCycles(uint32_t start_cycles)
{
    uint32_t elapsed;

    if (ad2s_dwt_ready == 0U)
    {
        return;
    }

    elapsed = (uint32_t)(DWT->CYCCNT - start_cycles);
    ad2s_acquisition_last_cycles = elapsed;
    if (elapsed > ad2s_acquisition_max_cycles)
    {
        ad2s_acquisition_max_cycles = elapsed;
    }
    if ((ad2s_acquisition_budget_cycles != 0U) &&
        (elapsed > ad2s_acquisition_budget_cycles))
    {
        ad2s_acquisition_over_budget_count++;
    }
}

static HAL_StatusTypeDef AD2S1210_Transfer8(uint8_t tx, uint8_t *rx)
{
    HAL_StatusTypeDef status;

    if (rx == NULL)
    {
        return HAL_ERROR;
    }

    /* Configuration runs before TIM1 starts. Runtime normal-frame reads
       use the same bounded HAL path in appTask; no SPI runs in TIM1.
       The timeout follows the independent HAL TIM7 time base. */
    HAL_GPIO_WritePin(AD2S1210_WR_PORT, AD2S1210_WR_PIN, GPIO_PIN_RESET);
    __NOP();
    __NOP();
    status = HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1U, 10U);
    HAL_GPIO_WritePin(AD2S1210_WR_PORT, AD2S1210_WR_PIN, GPIO_PIN_SET);
    AD2S1210_ShortDelay();
    return status;
}

static HAL_StatusTypeDef AD2S1210_ReadRegister(uint8_t address, uint8_t *value)
{
    HAL_StatusTypeDef status;
    uint8_t rx = 0U;

    if (value == NULL)
    {
        return HAL_ERROR;
    }

    AD2S1210_ModeConfig();
    AD2S1210_ShortDelay();

    status = AD2S1210_Transfer8(address, &rx);
    if (status != HAL_OK)
    {
        return status;
    }
    status = AD2S1210_Transfer8(address, &rx);
    if (status == HAL_OK)
    {
        *value = rx;
    }
    return status;
}

static HAL_StatusTypeDef AD2S1210_WriteRegister(uint8_t address, uint8_t data)
{
    HAL_StatusTypeDef status;
    uint8_t rx = 0U;

    if (((address & 0x80U) == 0U) || ((data & 0x80U) != 0U))
    {
        return HAL_ERROR;
    }

    AD2S1210_ModeConfig();
    AD2S1210_ShortDelay();
    status = AD2S1210_Transfer8(address, &rx);
    if (status != HAL_OK)
    {
        return status;
    }
    return AD2S1210_Transfer8(data, &rx);
}

static HAL_StatusTypeDef AD2S1210_ReadAndClearFault(uint8_t *fault)
{
    HAL_StatusTypeDef status;

    if (fault == NULL)
    {
        return HAL_ERROR;
    }

    /* Datasheet sequence: SAMPLE falling edge, address/read 0xFF, then a
       second SAMPLE falling edge clears the latched fault register.  The
       register remains the last addressed byte for normal-mode 24-bit reads. */
    AD2S1210_ModeConfig();
    AD2S1210_ShortDelay();
    AD2S1210_PulseSample(NULL, NULL);
    status = AD2S1210_ReadRegister(AD2S1210_REG_FAULT, fault);
    AD2S1210_PulseSample(NULL, NULL);
    return status;
}

static HAL_StatusTypeDef AD2S1210_ReadNormalHAL(uint8_t velocity_mode,
                                                uint16_t *angular_data,
                                                uint8_t *fault,
                                                uint32_t *sample_cycles,
                                                uint32_t *sample_tick_ms)
{
    HAL_StatusTypeDef status;
    uint8_t tx[3] = {0U, 0U, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};

    if ((angular_data == NULL) || (fault == NULL))
    {
        return HAL_ERROR;
    }

    if (velocity_mode != 0U)
    {
        AD2S1210_ModeVelocity();
    }
    else
    {
        AD2S1210_ModePosition();
    }
    AD2S1210_ShortDelay();
    AD2S1210_PulseSample(sample_cycles, sample_tick_ms);

    HAL_GPIO_WritePin(AD2S1210_WR_PORT, AD2S1210_WR_PIN, GPIO_PIN_RESET);
    __NOP();
    __NOP();
    status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3U, 10U);
    HAL_GPIO_WritePin(AD2S1210_WR_PORT, AD2S1210_WR_PIN, GPIO_PIN_SET);
    AD2S1210_ShortDelay();

    if (status == HAL_OK)
    {
        *angular_data = ((uint16_t)rx[0] << 8) | (uint16_t)rx[1];
        *fault = rx[2];
    }
    return status;
}

void AD2S1210_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_StatusTypeDef status;
    HAL_StatusTypeDef diagnose_status;
    uint16_t diagnose_position = 0U;
    uint8_t diagnose_fault = 0xFFU;
    uint8_t recovery_clear_fault = 0xFFU;
    AD2S1210_FeedbackSnapshot empty_snapshot = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    HAL_GPIO_WritePin(AD2S1210_WR_PORT, AD2S1210_WR_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(AD2S1210_SAMPLE_PORT, AD2S1210_SAMPLE_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(AD2S1210_A0_PORT, AD2S1210_A0_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(AD2S1210_A1_PORT, AD2S1210_A1_PIN, GPIO_PIN_SET);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    /* The control pins only switch around a 2 MHz serial frame.  Low slew
       matches the known-good diagnostic project and reduces PCB ringing. */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = AD2S1210_WR_PIN | AD2S1210_A0_PIN;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = AD2S1210_SAMPLE_PIN | AD2S1210_A1_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);

    ad2s_initialized = 0U;
    ad2s_feedback_valid = 0U;
    ad2s_fault = 0xFFU;
    ad2s_fault_initial = 0xFFU;
    ad2s_fault_after_reset = 0xFFU;
    ad2s_fault_first_direct = 0xFFU;
    ad2s_fault_config_confirm = 0xFFU;
    ad2s_fault_first_hal = 0xFFU;
    ad2s_fault_recovery_attempted = 0U;
    ad2s_fault_before_recovery = 0xFFU;
    ad2s_fault_after_recovery = 0xFFU;
    ad2s_fault_recovery_status = (uint8_t)HAL_ERROR;
    ad2s_fault_diagnose_status = (uint8_t)HAL_ERROR;
    ad2s_slow_link_diag_active = AD2S1210_SLOW_LINK_DIAG;
    /* SPI1 starts at the known-good 64 MHz / 256 = 250 kHz rate. */
    ad2s_spi_kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
    ad2s_spi_sclk_hz = ad2s_spi_kernel_hz / 256UL;
    ad2s_runtime_spi_status = (uint8_t)HAL_ERROR;
    ad2s_spi_cfg1_readback = hspi1.Instance->CFG1;
    ad2s_software_reset_status = (uint8_t)HAL_ERROR;
    ad2s_init_status = (uint8_t)HAL_ERROR;
    ad2s_config_parity_error_count = 0U;
    ad2s_config_error_address = 0U;
    ad2s_config_error_value = 0U;
    ad2s_config_repair_count = 0U;
    ad2s_config_repair_failed_count = 0U;
    ad2s_config_repair_error_address = 0U;
    ad2s_config_repair_error_value = 0U;
    ad2s_dos_overrange_readback = 0xFFU;
    ad2s_dos_mismatch_readback = 0xFFU;
    ad2s_lot_high_readback = 0xFFU;
    ad2s_lot_low_readback = 0xFFU;
    ad2s_threshold_config_status = (uint8_t)HAL_ERROR;
    ad2s_fault_latched = 0U;
    ad2s_position_data_valid = 0U;
    ad2s_valid_streak = 0U;
    ad2s_position_read_count = 0U;
    ad2s_velocity_read_count = 0U;
    ad2s_previous_position_raw = 0U;
    ad2s_previous_sample_cycles = 0U;
    ad2s_previous_sample_tick_ms = 0U;
    ad2s_position_speed_ready = 0U;
    ad2s_position_raw = 0U;
    ad2s_position_rad = 0.0f;
    ad2s_speed_rpm = 0.0f;
    ad2s_speed_rpm_filt = 0.0f;
    ad2s_spi_error_count = 0U;
    ad2s_snapshot_max_cycles = 0U;
    ad2s_sample_edge_max_cycles = 0U;
    ad2s_sample_interval_cycles = 0U;
    ad2s_sample_interval_max_cycles = 0U;
    ad2s_sample_gap_count = 0U;
    ad2s_acquisition_last_cycles = 0U;
    ad2s_acquisition_max_cycles = 0U;
    ad2s_acquisition_over_budget_count = 0U;
    AD2S1210_DWT_Init();
    empty_snapshot.fault = 0xFFU;
    AD2S1210_PublishSnapshot(&empty_snapshot);
    ad2s_acquisition_budget_cycles =
        (SystemCoreClock / 1000000U) * AD2S1210_ACQUISITION_BUDGET_US;

    /* RESET is not connected to the MCU, so allow the power-on reset to finish. */
    HAL_Delay(20U);

    status = AD2S1210_ReadRegister(AD2S1210_REG_CONTROL,
                                   (uint8_t *)&ad2s_control_before);
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_CONTROL,
                                        AD2S1210_CONTROL_16BIT);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(AD2S1210_REG_CONTROL,
                                       (uint8_t *)&ad2s_control_readback);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_EXC_FREQ,
                                        AD2S1210_EXC_FCW_8MHZ);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(AD2S1210_REG_EXC_FREQ,
                                       (uint8_t *)&ad2s_exc_readback);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_DOS_OVERRANGE,
                                        AD2S1210_DOS_OVERRANGE_RELAXED);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(
            AD2S1210_REG_DOS_OVERRANGE,
            (uint8_t *)&ad2s_dos_overrange_readback);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_DOS_MISMATCH,
                                        AD2S1210_DOS_MISMATCH_RELAXED);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(
            AD2S1210_REG_DOS_MISMATCH,
            (uint8_t *)&ad2s_dos_mismatch_readback);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_LOT_HIGH,
                                        AD2S1210_LOT_HIGH_RELAXED);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(
            AD2S1210_REG_LOT_HIGH,
            (uint8_t *)&ad2s_lot_high_readback);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_WriteRegister(AD2S1210_REG_LOT_LOW,
                                        AD2S1210_LOT_LOW_RELAXED);
    }
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadRegister(
            AD2S1210_REG_LOT_LOW,
            (uint8_t *)&ad2s_lot_low_readback);
    }
    if ((status == HAL_OK) &&
        (ad2s_dos_overrange_readback == AD2S1210_DOS_OVERRANGE_RELAXED) &&
        (ad2s_dos_mismatch_readback == AD2S1210_DOS_MISMATCH_RELAXED) &&
        (ad2s_lot_high_readback == AD2S1210_LOT_HIGH_RELAXED) &&
        (ad2s_lot_low_readback == AD2S1210_LOT_LOW_RELAXED))
    {
        ad2s_threshold_config_status = (uint8_t)HAL_OK;
    }
    else
    {
        status = HAL_ERROR;
    }

    ad2s_exc_frequency_hz =
        ((uint32_t)(ad2s_exc_readback & 0x7FU) * AD2S1210_CLKIN_HZ) / 32768UL;

    /* At 16-bit resolution the AD2S1210 tracking loop requires about 60 ms
       after reset or a tracking-configuration change.  Faults are expected
       during that interval, so wait with margin before the only read/clear. */
    if (status == HAL_OK)
    {
        HAL_Delay(100U);
    }

    /* Keep the primary path byte-for-byte equivalent to the standalone project
       verified on this PCB: configure CONTROL and EXC, then address/read/clear
       the fault register once.  A second read/clear is permitted only after a
       failed normal-frame confirmation and a long power-domain settling wait.
       Never scan/rewrite the remaining configuration registers here. */
    if (status == HAL_OK)
    {
        status = AD2S1210_ReadAndClearFault(&diagnose_fault);
        if (status == HAL_OK)
        {
            ad2s_fault_initial = diagnose_fault;
            ad2s_fault_config_confirm = diagnose_fault;
        }
    }

    if ((status == HAL_OK) &&
        (ad2s_control_readback == AD2S1210_CONTROL_16BIT) &&
        ((ad2s_exc_readback & 0x7FU) == AD2S1210_EXC_FCW_8MHZ) &&
        (ad2s_threshold_config_status == (uint8_t)HAL_OK))
    {
        ad2s_initialized = 1U;
    }
    ad2s_init_status = (uint8_t)status;

    /* The fault register is still the last addressed configuration register.
       Read one normal-mode 24-bit frame through the same 250 kHz HAL path used
       by the known-good standalone test.  Do not change the SPI rate here. */
    AD2S1210_ModePosition();
    HAL_Delay(2U);
    diagnose_status = status;
    if (diagnose_status == HAL_OK)
    {
        diagnose_status = AD2S1210_ReadNormalHAL(0U,
                                                 &diagnose_position,
                                                 &diagnose_fault,
                                                 NULL, NULL);
    }
    if (diagnose_status == HAL_OK)
    {
        ad2s_fault_first_hal = diagnose_fault;
        ad2s_fault = diagnose_fault;
        ad2s_fault_latched = diagnose_fault;

        if (diagnose_fault != 0U)
        {
            /* A 24 V power-domain startup transient can latch D7 even when the
               settled differential SIN/COS waveforms are valid.  Leave all
               configuration registers untouched, wait for every rail to
               settle, then perform one explicit fault read/clear and confirm
               it with one normal-mode frame. */
            ad2s_fault_recovery_attempted = 1U;
            ad2s_fault_before_recovery = diagnose_fault;
            HAL_Delay(1000U);

            diagnose_status = AD2S1210_ReadAndClearFault(
                &recovery_clear_fault);
            if (diagnose_status == HAL_OK)
            {
                AD2S1210_ModePosition();
                HAL_Delay(2U);
                diagnose_status = AD2S1210_ReadNormalHAL(
                    0U, &diagnose_position, &diagnose_fault, NULL, NULL);
            }

            if (diagnose_status == HAL_OK)
            {
                ad2s_fault_after_recovery = diagnose_fault;
                if (diagnose_fault == 0U)
                {
                    ad2s_fault = 0U;
                    ad2s_fault_latched = 0U;
                    ad2s_fault_recovery_status = (uint8_t)HAL_OK;
                }
                else
                {
                    ad2s_fault = diagnose_fault;
                    ad2s_fault_latched = diagnose_fault;
                    diagnose_status = HAL_ERROR;
                }
            }

            if (diagnose_status != HAL_OK)
            {
                ad2s_fault_recovery_status = (uint8_t)HAL_ERROR;
            }
        }
        else
        {
            ad2s_fault_after_recovery = 0U;
            ad2s_fault_recovery_status = (uint8_t)HAL_OK;
        }
    }
    ad2s_runtime_spi_status = (uint8_t)diagnose_status;
    /* Preserve the actual CONTROL/EXC/threshold configuration result.
       A failed initialization must not be forced to success to keep polling.
       Device fault bits remain separate diagnostics in this migration. */
    ad2s_fault_diagnose_status = (uint8_t)diagnose_status;
    AD2S1210_ModePosition();
#if (AD2S1210_SLOW_LINK_DIAG != 0U)
    ad2s_feedback_valid = 0U;
#endif
}

void AD2S1210_Sample(void)
{
    /* Compatibility entry point: a single task-owned acquisition pipeline. */
    AD2S1210_Run_1kHz();
}

void AD2S1210_Run_1kHz(void)
{
    AD2S1210_FeedbackSnapshot snapshot;
    uint32_t start_cycles = DWT->CYCCNT;
#if (AD2S1210_SLOW_LINK_DIAG == 0U)
    HAL_StatusTypeDef status;
    uint16_t position_u16;
    uint8_t position_fault;
    uint32_t sample_cycles = 0U;
    uint32_t sample_tick_ms = 0U;
    float rpm;
#endif

    AD2S1210_GetSnapshot(&snapshot);
    snapshot.sequence++;
#if (AD2S1210_SLOW_LINK_DIAG != 0U)
    goto acquisition_invalid;
#else
    if (ad2s_initialized == 0U)
    {
        goto acquisition_invalid;
    }

    /* SPI/GPIO delays and float calculations remain outside publication. */
    status = AD2S1210_ReadNormalHAL(0U, &position_u16, &position_fault,
                                     &sample_cycles, &sample_tick_ms);
    ad2s_runtime_spi_status = (uint8_t)status;
    if (status != HAL_OK)
    {
        ad2s_spi_error_count++;
        goto acquisition_invalid;
    }

    ad2s_position_raw = position_u16;
    ad2s_position_data_valid = 1U;
    ad2s_position_rad = (float)position_u16 * AD2S1210_POSITION_SCALE_RAD;
    rpm = AD2S1210_PositionDeltaToMechanicalRPM(position_u16,
                sample_cycles, sample_tick_ms, &snapshot.speed_valid);
    ad2s_speed_rpm = rpm;
    if (snapshot.speed_valid == 0U)
    {
        /* First/recovery frame has position but no measured speed. */
        ad2s_speed_rpm_filt = 0.0f;
    }
    else
    {
        ad2s_speed_rpm_filt += AD2S1210_SPEED_FILTER_ALPHA *
                               (rpm - ad2s_speed_rpm_filt);
    }
    ad2s_position_read_count++;
    ad2s_velocity_read_count++;
    AD2S1210_UpdateFeedbackHealth(position_fault);
    snapshot.position_rad = ad2s_position_rad;
    snapshot.speed_rpm = ad2s_speed_rpm_filt;
    snapshot.sample_cycles = sample_cycles;
    snapshot.sample_tick_ms = sample_tick_ms;
    snapshot.valid = 1U;
    snapshot.fault = position_fault;
    AD2S1210_PublishSnapshot(&snapshot);
    AD2S1210_RecordAcquisitionCycles(start_cycles);
    return;
#endif

acquisition_invalid:
    ad2s_position_data_valid = 0U;
    ad2s_valid_streak = 0U;
    ad2s_feedback_valid = 0U;
    ad2s_position_speed_ready = 0U;
    ad2s_speed_rpm = 0.0f;
    ad2s_speed_rpm_filt = 0.0f;
    snapshot.valid = 0U;
    snapshot.speed_valid = 0U;
    snapshot.speed_rpm = 0.0f;
    /* Old position/time/fault remain diagnostic only; valid=0 makes their
       unusable state explicit. A new pair of good frames rebuilds speed. */
    AD2S1210_PublishSnapshot(&snapshot);
    AD2S1210_RecordAcquisitionCycles(start_cycles);
}

void AD2S1210_Run_20kHz(void)
{
    /* Intentionally empty. appTask acquires position/speed; TIM1 reads a
       complete AD2S1210_GetSnapshot() copy. No polling SPI belongs here. */
}

uint16_t AD2S1210_ReadPosition(void)
{
    return ad2s_position_raw;
}

int16_t AD2S1210_ReadVelocity(void)
{
    return ad2s_velocity_raw;
}

uint8_t AD2S1210_ReadFault(void)
{
    return ad2s_fault;
}

float AD2S1210_VelocityToMechanicalRPM(int16_t velocity_raw)
{
    return ((float)velocity_raw / 32768.0f) *
           AD2S1210_VEL_MAX_RPS_16BIT * 60.0f /
           AD2S1210_RESOLVER_POLE_PAIRS;
}

float AD2S1210_GetRealtimeSpeedRPM(void)
{
    return ad2s_speed_rpm_filt;
}

uint8_t AD2S1210_IsInitialized(void)
{
    return ad2s_initialized;
}

uint8_t AD2S1210_IsFeedbackValid(void)
{
    return ad2s_feedback_valid;
}

uint8_t AD2S1210_IsPositionDataValid(void)
{
    return ad2s_position_data_valid;
}
