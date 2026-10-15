#ifndef __OPEN_FOC_H
#define __OPEN_FOC_H

#include <stdint.h>

typedef enum
{
    FOC_CONTROL_CURRENT_LOOP = 0,
    FOC_CONTROL_SPEED_CURRENT_LOOP = 1,
    FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST = 2
} FOC_ControlState;

void OpenFOC_Init(void);
void OpenFOC_Run_20kHz(void);
void OpenFOC_Run_1ms(void);
void OpenFOC_Ramp_1ms(void);
void OpenFOC_SetTargetRPM(float rpm);
void OpenFOC_SetVoltageNorm(float uq_norm);
void OpenFOC_SetUqVoltageNorm(float uq_norm);
void OpenFOC_SetRotorFeedback(float resolver_angle_rad,
                              float resolver_speed_rpm,
                              uint8_t valid);
void OpenFOC_SetIdFeedback(float id_a, uint8_t valid);
void OpenFOC_SetCurrentFeedback(float id_a, float iq_a, uint8_t valid);
void OpenFOC_Stop(void);

extern volatile uint8_t foc_run_request;
extern volatile FOC_ControlState foc_control_state_request;
extern volatile FOC_ControlState foc_control_state_active;

extern volatile float foc_du;
extern volatile float foc_dv;
extern volatile float foc_dw;
extern volatile float foc_theta;
extern volatile float foc_current_rpm;
extern volatile uint8_t foc_aligning;
extern volatile float foc_align_u_norm;
extern volatile uint8_t foc_auto_calibrate_offset;
extern volatile float foc_resolver_direction;
extern volatile float foc_speed_feedback_direction;
extern volatile float foc_theta_offset_rad;

extern volatile float foc_id_ref_a;
extern volatile float foc_current_loop_iq_ref_a;
extern volatile float foc_current_loop_uq_ff_norm;
extern volatile float foc_iq_ref_a;
extern volatile float foc_kp_d;
extern volatile float foc_ki_d;
extern volatile float foc_kp_q;
extern volatile float foc_ki_q;
extern volatile float foc_ud_pi;
extern volatile float foc_uq_pi;
extern volatile float foc_uq_ff;
extern volatile float foc_uq_open_loop_norm;
extern volatile float foc_id_integrator;
extern volatile float foc_iq_integrator;
extern volatile float foc_current_pi_u_limit_norm;
extern volatile float foc_current_pi_i_limit_norm;
extern volatile float foc_current_test_ref_limit_a;

extern volatile float foc_speed_target_rpm;
extern volatile float foc_speed_ref_rpm;
extern volatile float foc_speed_feedback_rpm;
extern volatile float foc_kp_speed;
extern volatile float foc_ki_speed;
extern volatile float foc_speed_integrator;
extern volatile float foc_speed_pi_iq_ref_a;
extern volatile float foc_speed_iq_limit_a;
extern volatile int8_t foc_speed_step_command;

/* Resolver/open-loop commissioning state (state 2).
   Pole-pair estimate = |electrical rpm command| / |resolver mechanical rpm|. */
extern volatile float foc_open_loop_test_speed_rpm;
extern volatile float foc_open_loop_test_pole_pairs;
extern volatile float foc_open_loop_electrical_rpm;
extern volatile float foc_pole_pair_estimate_raw;
extern volatile float foc_pole_pair_estimate;
extern volatile uint8_t foc_pole_pair_estimate_valid;

#endif
