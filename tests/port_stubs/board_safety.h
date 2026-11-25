#ifndef COMMAND_PORT_HOST_BOARD_SAFETY_H
#define COMMAND_PORT_HOST_BOARD_SAFETY_H
#include <stdint.h>
extern volatile uint32_t board_fault_mask;
extern volatile uint8_t board_output_enabled;
void BoardSafety_DisableOutputs(void);
#endif
