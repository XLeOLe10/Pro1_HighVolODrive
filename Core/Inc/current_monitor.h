#ifndef __CURRENT_MONITOR_H
#define __CURRENT_MONITOR_H

#include <stdint.h>
#include "board_config.h"

/* 20 kHz current acquisition and Clarke/Park feedback for the FOC loops. */
void CurrentMonitor_Init(void);
#if AXIS_COMMAND_BENCH_ENABLE
/* TIM1 calls once only for a successful early read; no ADC access or raw cache.
 * The caller supplies the angle at the original current-processing point. */
void CurrentMonitor_ProcessRaw_20kHz(float electrical_angle_rad,
                                    uint16_t raw_u, uint16_t raw_v,
                                    uint16_t raw_w, uint16_t raw_bus);
#else
void CurrentMonitor_Run_20kHz(float electrical_angle_rad);
#endif
uint8_t CurrentMonitor_IsReady(void);

extern volatile uint32_t monitor_sample_sequence;
extern volatile float monitor_ia_a;
extern volatile float monitor_ia_adc_a;
extern volatile float monitor_ib_a;
extern volatile float monitor_ic_a;
extern volatile float monitor_vbus_v;
extern volatile float monitor_i_alpha_a;
extern volatile float monitor_i_beta_a;
extern volatile float monitor_id_a;
extern volatile float monitor_iq_a;

#endif /* __CURRENT_MONITOR_H */
