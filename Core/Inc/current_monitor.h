#ifndef __CURRENT_MONITOR_H
#define __CURRENT_MONITOR_H

#include <stdint.h>

/* 20 kHz current acquisition and Clarke/Park feedback for the FOC loops. */
void CurrentMonitor_Init(void);
void CurrentMonitor_Run_20kHz(float electrical_angle_rad);
uint8_t CurrentMonitor_IsReady(void);

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
