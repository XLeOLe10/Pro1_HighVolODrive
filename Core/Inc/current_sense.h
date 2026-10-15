#ifndef __CURRENT_SENSE_H
#define __CURRENT_SENSE_H

#include <stdint.h>

/*
 * STM32H750VBT6 current-sense mapping:
 *   U phase: PA0 / ADC1_INP16, injected rank 1
 *   V phase: PA5 / ADC2_INP19, injected rank 1
 *   W phase: PC0 / ADC1_INP10, injected rank 2
 *   DC bus:  PC3 / ADC3_INP1,  injected rank 1
 *
 * ADC1, ADC2 and ADC3 are armed once and triggered every PWM period from
 * TIM1_TRGO2 (the internal channel-4 OC4REF rising edge). U, V and VBUS start
 * at the same instant; W follows U as ADC1 injected rank 2. No DMA is used,
 * so Cortex-M7 data-cache coherency does not enter the 20 kHz current path.
 */

void CurrentSense_Init(void);
void CurrentSense_Start(void);
uint8_t CurrentSense_HasNewSample(void);
void CurrentSense_GetRaw(uint16_t *iu, uint16_t *iv, uint16_t *iw, uint16_t *vbus);

#endif /* __CURRENT_SENSE_H */
