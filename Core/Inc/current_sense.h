#ifndef __CURRENT_SENSE_H
#define __CURRENT_SENSE_H

#include <stdint.h>
#include "board_config.h"

#if AXIS_COMMAND_BENCH_ENABLE
/* First active-command ADC miss, frozen only after the caller disarms.
 * first_missing_adc=1/2/3 is the first absent JEOS in the real short-circuit
 * check; later ADCs were NOT checked. missing_isr is that exact register read.
 * Intervals use check-start DWT samples, not conversion/IRQ-entry timestamps.
 * UINT32_MAX means the required earlier check/body has not been observed.
 * tim_phase is a later, non-atomic CR1/CNT/CR1 observation: CNT in bits0..15,
 * final DIR in bit16, bit31 set if DIR changed around the CNT read.
 * The trace is diagnostic only; read fields only after captured=1. */
typedef struct {
    uint32_t captured;
    uint32_t first_missing_adc;
    uint32_t missing_isr;
    uint32_t check_interval_cycles;
    uint32_t since_success_cycles;
    uint32_t body_entry_interval_cycles;
    uint32_t entry_to_check_cycles;
    uint32_t previous_body_cycles;
    uint32_t tim_phase;
    uint32_t tim_sr;
    uint32_t command_seq;
} CurrentSenseFaultTrace;
extern volatile CurrentSenseFaultTrace current_sense_fault_trace;
void CurrentSense_CaptureFault(uint32_t body_entry_cycles,
                              uint32_t previous_body_entry_cycles,
                              uint32_t previous_body_cycles,
                              uint32_t previous_body_valid,
                              uint32_t command_seq);
#endif

/*
 * STM32H750VBT6 current-sense mapping:
 *   U phase: PA0 / ADC1_INP16, injected rank 1
 *   V phase: PA5 / ADC2_INP19, injected rank 1
 *   W phase: PC0 / ADC3_INP10, injected rank 1
 *   DC bus:  PC3_C / ADC3_INP1, injected rank 2
 *
 * ADC1, ADC2 and ADC3 are armed once and triggered every PWM period from
 * TIM1_TRGO2 (the internal channel-4 OC4REF rising edge). U, V and W occupy
 * rank 1 of the three ADCs; VBUS follows W in ADC3 rank 2. No DMA is used,
 * so Cortex-M7 data-cache coherency does not enter the 20 kHz current path.
 */

void CurrentSense_Init(void);
void CurrentSense_Start(void);
uint8_t CurrentSense_HasNewSample(void);
void CurrentSense_GetRaw(uint16_t *iu, uint16_t *iv, uint16_t *iw, uint16_t *vbus);

#endif /* __CURRENT_SENSE_H */
