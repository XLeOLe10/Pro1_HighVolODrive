#include "axis_status.h"

volatile uint32_t axis_status_word = AXIS_STATUS_PACK(AXIS_OBS_BOOT, 0U);

void AxisStatus_Publish(uint32_t enabled, uint32_t aligning, uint32_t inhibits)
{
    uint32_t previous = axis_status_word;
    /* Fault observations are retained until reset, just like board_fault_mask. */
    axis_status_word = AXIS_STATUS_NEXT(previous, enabled, aligning, inhibits);
}

void AxisStatus_LatchFault(void)
{
    uint32_t previous = axis_status_word;
    axis_status_word = AXIS_STATUS_ON_FAULT(previous);
}
