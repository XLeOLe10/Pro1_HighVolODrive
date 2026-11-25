#include "current_monitor.h"
#include "current_sense.h"
#include "Xmath.h"

/* CURRENT MONITOR PROCESS BEGIN: tested from this production block on the host. */
/* Board scaling: CC6905S8-3FB050 output sensitivity is 26.4 mV/A. */
#define CURRENT_MONITOR_VREF_V              3.300f
#define CURRENT_MONITOR_ADC_FULL_SCALE       4096.0f
#define CURRENT_MONITOR_SENSOR_SENS_V_PER_A  0.0264f
#define CURRENT_MONITOR_BUS_DIVIDER_RATIO    301.0f

/* The observed D current is negative with the board's sensor polarity. */
#define CURRENT_MONITOR_IU_SIGN               (-1.0f)
#define CURRENT_MONITOR_IV_SIGN               (-1.0f)
#define CURRENT_MONITOR_IW_SIGN               (-1.0f)

/* 0.1 s settling plus 0.205 s average, with the gate driver held disabled. */
#define CURRENT_MONITOR_SETTLE_SAMPLES       2000UL
#define CURRENT_MONITOR_CAL_SAMPLES          4096UL

volatile float monitor_ia_a = 0.0f;
volatile float monitor_ia_adc_a = 0.0f;
volatile float monitor_ib_a = 0.0f;
volatile float monitor_ic_a = 0.0f;
volatile float monitor_vbus_v = 0.0f;
volatile float monitor_i_alpha_a = 0.0f;
volatile float monitor_i_beta_a = 0.0f;
volatile float monitor_id_a = 0.0f;
volatile float monitor_iq_a = 0.0f;

static float offset_u;
static float offset_v;
static float offset_w;
static uint32_t settle_count;
static uint32_t cal_count;
static uint32_t sum_u;
static uint32_t sum_v;
static uint32_t sum_w;
static volatile uint8_t monitor_ready;
volatile uint32_t monitor_sample_sequence = 0U;

static float CurrentMonitor_CodeToAmp(uint16_t raw, float offset)
{
    return ((float)raw - offset) *
           (CURRENT_MONITOR_VREF_V / CURRENT_MONITOR_ADC_FULL_SCALE /
            CURRENT_MONITOR_SENSOR_SENS_V_PER_A);
}

void CurrentMonitor_Init(void)
{
    monitor_ia_a = 0.0f;
    monitor_ia_adc_a = 0.0f;
    monitor_ib_a = 0.0f;
    monitor_ic_a = 0.0f;
    monitor_vbus_v = 0.0f;
    monitor_i_alpha_a = 0.0f;
    monitor_i_beta_a = 0.0f;
    monitor_id_a = 0.0f;
    monitor_iq_a = 0.0f;
    offset_u = 0.0f;
    offset_v = 0.0f;
    offset_w = 0.0f;
    settle_count = 0U;
    cal_count = 0U;
    sum_u = 0U;
    sum_v = 0U;
    sum_w = 0U;
    monitor_ready = 0U;
    monitor_sample_sequence = 0U;
}

uint8_t CurrentMonitor_IsReady(void)
{
    return monitor_ready;
}

#if AXIS_COMMAND_BENCH_ENABLE
void CurrentMonitor_ProcessRaw_20kHz(float electrical_angle_rad,
                                    uint16_t raw_u, uint16_t raw_v,
                                    uint16_t raw_w, uint16_t raw_bus)
#else
void CurrentMonitor_Run_20kHz(float electrical_angle_rad)
#endif
{
#if !AXIS_COMMAND_BENCH_ENABLE
    uint16_t raw_u;
    uint16_t raw_v;
    uint16_t raw_w;
    uint16_t raw_bus;
#endif
    float ia;
    float ib;
    float ic;
    float sine;
    float cosine;
    float i_alpha;
    float i_beta;
    float id;
    float iq;

#if !AXIS_COMMAND_BENCH_ENABLE
    if (CurrentSense_HasNewSample() == 0U)
    {
        return;
    }

    CurrentSense_GetRaw(&raw_u, &raw_v, &raw_w, &raw_bus);
#endif
    monitor_sample_sequence++; /* TIM1-owned; confirms this iteration consumed a frame. */
    monitor_vbus_v = (float)raw_bus * CURRENT_MONITOR_VREF_V /
                     CURRENT_MONITOR_ADC_FULL_SCALE *
                     CURRENT_MONITOR_BUS_DIVIDER_RATIO;

    if (monitor_ready == 0U)
    {
        if (settle_count < CURRENT_MONITOR_SETTLE_SAMPLES)
        {
            settle_count++;
            return;
        }

        sum_u += raw_u;
        sum_v += raw_v;
        sum_w += raw_w;
        cal_count++;

        if (cal_count >= CURRENT_MONITOR_CAL_SAMPLES)
        {
            offset_u = (float)sum_u / (float)cal_count;
            offset_v = (float)sum_v / (float)cal_count;
            offset_w = (float)sum_w / (float)cal_count;
            monitor_ready = 1U;
        }
        return;
    }

    /* Keep the PA0 measurement for diagnosis, but do not use it for control. */
    monitor_ia_adc_a = CurrentMonitor_CodeToAmp(raw_u, offset_u) * CURRENT_MONITOR_IU_SIGN;
    monitor_ib_a = CurrentMonitor_CodeToAmp(raw_v, offset_v) * CURRENT_MONITOR_IV_SIGN;
    monitor_ic_a = CurrentMonitor_CodeToAmp(raw_w, offset_w) * CURRENT_MONITOR_IW_SIGN;

    /* The noisy U-phase sensor is reconstructed from Kirchhoff's current law. */
    monitor_ia_a = -(monitor_ib_a + monitor_ic_a);

    /*
     * Ib and Ic are the two measured currents. Ia is reconstructed above, so
     * ia + ib + ic is exactly zero before the Clarke/Park transforms.
     */
    ia = monitor_ia_a;
    ib = monitor_ib_a;
    ic = monitor_ic_a;
    (void)ic;

    clarke(&i_alpha, &i_beta, ia, ib);
    fast_sin_cos(electrical_angle_rad, &sine, &cosine);
    park(&id, &iq, i_alpha, i_beta, sine, cosine);
    monitor_i_alpha_a = i_alpha;
    monitor_i_beta_a = i_beta;
    monitor_id_a = id;
    monitor_iq_a = iq;
}
/* CURRENT MONITOR PROCESS END */
