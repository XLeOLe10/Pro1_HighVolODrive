#ifndef __AD2S1210_H
#define __AD2S1210_H

#include <stdint.h>
#include "board_config.h"

/* appTask owns acquisition. TIM1 consumes this complete frame.
   Timestamps refer to the SAMPLE falling edge. valid describes a position
   transaction; the AD2S fault bits remain a separate diagnostic policy. */
typedef struct
{
    float position_rad;
    float speed_rpm;
    uint32_t sample_cycles;
    uint32_t sample_tick_ms;
    uint32_t sequence;
    uint32_t valid;
    uint32_t speed_valid;
    uint32_t fault;
} AD2S1210_FeedbackSnapshot;

#if AXIS_COMMAND_BENCH_ENABLE
enum {
    AD2S_AGE_IDLE = 0,
    AD2S_AGE_PENDING,
    AD2S_AGE_EDGE_BY_CHECK,
    AD2S_AGE_EDGE_AFTER_CHECK,
    AD2S_AGE_INVALID_NEXT,
    AD2S_AGE_UNKNOWN
};

/* TIM1 arms once with its actual ready-decision sample. The publishing task
   completes the record for the adjacent attempt. Read state before intervals.
   EDGE_BY_CHECK includes equality of software timestamps. */
typedef struct {
    uint32_t state;
    uint32_t source_sequence;
    uint32_t next_sequence;
    uint32_t check_age_cycles;
    uint32_t edge_interval_cycles;
    uint32_t edge_to_publish_cycles;
} AD2S1210_AgeTrace;
extern volatile AD2S1210_AgeTrace ad2s_age_trace;
void AD2S1210_ArmAgeTrace(const AD2S1210_FeedbackSnapshot *source,
                         uint32_t check_cycles);
#endif

/* Bounded PRIMASK-protected copy, no RTOS call or waiting for the writer. */
void AD2S1210_GetSnapshot(AD2S1210_FeedbackSnapshot *snapshot);


void AD2S1210_Init(void);
/* Serialized acquisition: Init before scheduling, then only appTask. */
void AD2S1210_Sample(void);
void AD2S1210_Run_1kHz(void);
void AD2S1210_Run_20kHz(void);

uint16_t AD2S1210_ReadPosition(void);
int16_t AD2S1210_ReadVelocity(void);
uint8_t AD2S1210_ReadFault(void);
float AD2S1210_VelocityToMechanicalRPM(int16_t velocity_raw);
float AD2S1210_GetRealtimeSpeedRPM(void);
uint8_t AD2S1210_IsInitialized(void);
uint8_t AD2S1210_IsFeedbackValid(void);
uint8_t AD2S1210_IsPositionDataValid(void);

extern volatile uint16_t ad2s_position_raw;
extern volatile int16_t ad2s_velocity_raw;
extern volatile float ad2s_position_rad;
extern volatile float ad2s_speed_rpm;
extern volatile float ad2s_speed_rpm_filt;
extern volatile uint8_t ad2s_fault;
extern volatile uint8_t ad2s_fault_initial;
extern volatile uint8_t ad2s_fault_after_reset;
extern volatile uint8_t ad2s_fault_first_direct;
extern volatile uint8_t ad2s_fault_config_confirm;
extern volatile uint8_t ad2s_fault_first_hal;
extern volatile uint8_t ad2s_fault_recovery_attempted;
extern volatile uint8_t ad2s_fault_before_recovery;
extern volatile uint8_t ad2s_fault_after_recovery;
extern volatile uint8_t ad2s_fault_recovery_status;
extern volatile uint8_t ad2s_fault_diagnose_status;
extern volatile uint8_t ad2s_slow_link_diag_active;
extern volatile uint32_t ad2s_spi_kernel_hz;
extern volatile uint32_t ad2s_spi_sclk_hz;
extern volatile uint8_t ad2s_runtime_spi_status;
extern volatile uint32_t ad2s_spi_cfg1_readback;
extern volatile uint8_t ad2s_software_reset_status;
extern volatile uint8_t ad2s_init_status;
extern volatile uint8_t ad2s_config_parity_error_count;
extern volatile uint8_t ad2s_config_error_address;
extern volatile uint8_t ad2s_config_error_value;
extern volatile uint8_t ad2s_config_repair_count;
extern volatile uint8_t ad2s_config_repair_failed_count;
extern volatile uint8_t ad2s_config_repair_error_address;
extern volatile uint8_t ad2s_config_repair_error_value;
extern volatile uint8_t ad2s_control_before;
extern volatile uint8_t ad2s_control_readback;
extern volatile uint8_t ad2s_exc_readback;
extern volatile uint8_t ad2s_dos_overrange_readback;
extern volatile uint8_t ad2s_dos_mismatch_readback;
extern volatile uint8_t ad2s_lot_high_readback;
extern volatile uint8_t ad2s_lot_low_readback;
extern volatile uint8_t ad2s_threshold_config_status;
extern volatile uint32_t ad2s_exc_frequency_hz;
extern volatile uint8_t ad2s_initialized;
extern volatile uint8_t ad2s_feedback_valid;
extern volatile uint8_t ad2s_position_data_valid;
extern volatile uint8_t ad2s_fault_latched;
extern volatile uint16_t ad2s_valid_streak;
extern volatile uint32_t ad2s_position_read_count;
extern volatile uint32_t ad2s_velocity_read_count;
extern volatile uint32_t ad2s_spi_error_count;
extern volatile uint8_t ad2s_dwt_ready;
extern volatile uint32_t ad2s_acquisition_last_cycles;
extern volatile uint32_t ad2s_acquisition_max_cycles;
extern volatile uint32_t ad2s_acquisition_budget_cycles;
extern volatile uint32_t ad2s_acquisition_over_budget_count;
/* Body timing excludes PRIMASK prologue/restore and statistics update.
   Complete interrupt latency still needs a board measurement. */
extern volatile uint32_t ad2s_snapshot_max_cycles;
extern volatile uint32_t ad2s_sample_edge_max_cycles;
extern volatile uint32_t ad2s_sample_interval_cycles;
extern volatile uint32_t ad2s_sample_interval_max_cycles;
extern volatile uint32_t ad2s_sample_gap_count;

#endif
