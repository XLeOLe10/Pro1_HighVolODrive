#ifndef BOARD_SAFETY_H
#define BOARD_SAFETY_H
#include <stdint.h>

enum {
    BOARD_FAULT_HAL          = 1UL << 0,
    BOARD_FAULT_KERNEL      = 1UL << 1,
    BOARD_FAULT_TASK_CREATE = 1UL << 2,
    BOARD_FAULT_MALLOC      = 1UL << 3,
    BOARD_FAULT_STACK       = 1UL << 4,
    BOARD_FAULT_ASSERT      = 1UL << 5,
    BOARD_FAULT_CPU         = 1UL << 6,
    BOARD_FAULT_SENSOR_INIT = 1UL << 7,
    BOARD_FAULT_FEEDBACK    = 1UL << 8,
    BOARD_FAULT_ADC_FRAME   = 1UL << 9,
    BOARD_FAULT_SCHEDULER   = 1UL << 10
};
extern volatile uint32_t board_fault_mask;
extern volatile uint32_t board_first_fault;
extern volatile uint32_t board_fault_ipsr;
extern volatile uint8_t board_output_enabled;

void BoardSafety_InitSafe(void);
void BoardSafety_DisableOutputs(void);
/* Only TIM1 may request enable; caller holds a short PRIMASK section.
 * Startup/task code may only disable. No HAL/RTOS calls inside this function. */
void BoardSafety_SetOutputsEnabled(uint8_t enable);
/* Irreversible until MCU reset; clears EN/MOE and CH1-3 enables, preserves CH4. */
void BoardSafety_LatchFault(uint32_t reason);
#if defined(__CC_ARM)
__declspec(noreturn) void BoardSafety_Fatal(uint32_t reason);
#elif defined(__GNUC__)
__attribute__((noreturn)) void BoardSafety_Fatal(uint32_t reason);
#else
void BoardSafety_Fatal(uint32_t reason);
#endif
#endif
