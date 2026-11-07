#include "axis_command.h"
#include "main.h"
#include "board_safety.h"
#include "cmsis_os.h"

/* Intentionally unconfirmed. These zeroes mean reject, not unlimited.
 * A later reviewed configuration must define real speed/current/lease bounds.
 * HOST tests supply synthetic limits; those are never linked here. */
static const AxisCommandLimits command_limits = {0U, 0.0f, 0.0f, 0U};
static AxisCommandCore command_core;
extern osThreadId_t appTaskHandle;

volatile uint32_t axis_command_configured;
volatile uint32_t axis_command_epoch;
volatile uint32_t axis_command_permit;
volatile uint32_t axis_command_applied_seq;
volatile uint32_t axis_command_result;
volatile uint32_t axis_command_result_seq;
volatile uint32_t axis_command_cancelled_seq;
volatile uint32_t axis_command_stop_applied;
volatile uint32_t axis_command_control_seq;
volatile uint32_t axis_command_critical_max_cycles;

static uint32_t IsPublisher(void)
{
    if (__get_IPSR() != 0U) return 0U;
    if (osKernelGetState() != osKernelRunning) return 0U;
    return (uint32_t)(osThreadGetId() == appTaskHandle);
}

static void Unlock(uint32_t saved, uint32_t started)
{
    uint32_t elapsed;
    __DMB();
    elapsed = (uint32_t)(DWT->CYCCNT - started);
    if (elapsed > axis_command_critical_max_cycles)
        axis_command_critical_max_cycles = elapsed;
    __set_PRIMASK(saved);
}

/* Diagnostic scalar mirrors, not a coherent external multiword snapshot.
 * Use function return values for task request acknowledgements. */
static void PublishDiagnostics(uint32_t now)
{
    axis_command_configured = command_core.config_valid;
    axis_command_epoch = command_core.run_epoch;
    axis_command_permit = AxisCommand_Permitted(&command_core, now, board_fault_mask);
    axis_command_applied_seq = command_core.applied_seq;
    axis_command_result = command_core.result;
    axis_command_result_seq = command_core.result_seq;
    axis_command_cancelled_seq = command_core.cancelled_seq;
    axis_command_stop_applied = command_core.stop_applied_ticket;
    axis_command_control_seq = command_core.control_seq;
}

void AxisCommandPort_Init(void)
{
    AxisCommand_Init(&command_core, &command_limits);
    PublishDiagnostics(HAL_GetTick());
}

uint32_t AxisCommandPort_Submit(const AxisCommandFrame *request, uint32_t *seq)
{
    AxisCommandFrame local;
    uint32_t result, saved, started;
    if (seq != NULL) *seq = 0U;
    if ((request == NULL) || (seq == NULL) || (IsPublisher() == 0U))
        return AXIS_CMD_INVALID;
    /* Caller owns request: no DMA/debugger is permitted to change it in place. */
    local = *request;
    result = AxisCommand_Validate(&command_limits, &local, HAL_GetTick());
    if (result != AXIS_CMD_OK) return result;
    saved = __get_PRIMASK();
    __disable_irq();
    started = DWT->CYCCNT;
    result = AxisCommand_PublishValidated(&command_core, &local, HAL_GetTick(),
                                          board_fault_mask, seq);
    Unlock(saved, started);
    return result;
}

uint32_t AxisCommandPort_Stop(uint32_t *ticket)
{
    uint32_t result, saved, started;
    if (ticket != NULL) *ticket = 0U;
    if ((ticket == NULL) || (IsPublisher() == 0U)) return AXIS_CMD_INVALID;
    saved = __get_PRIMASK();
    __disable_irq();
    started = DWT->CYCCNT;
    result = AxisCommand_RequestStop(&command_core, ticket);
    Unlock(saved, started);
    return result;
}

uint32_t AxisCommandPort_GetEpoch(void)
{
    uint32_t saved = __get_PRIMASK();
    uint32_t epoch, started;
    __disable_irq();
    started = DWT->CYCCNT;
    epoch = command_core.run_epoch;
    Unlock(saved, started);
    return epoch;
}

const AxisCommandFrame *AxisCommandPort_BeginCycle(uint32_t now, uint32_t ready)
{
    if (((command_core.active_valid != 0U) && (ready == 0U)) ||
        ((AxisCommand_NeedsDisarm(&command_core, now, board_fault_mask) != 0U) &&
         ((command_core.active_valid != 0U) || (command_core.stop_pending != 0U) ||
          (board_output_enabled != 0U))))
        BoardSafety_DisableOutputs();
    AxisCommand_Tick(&command_core, now, board_fault_mask, ready);
    return command_core.active_valid != 0U ? &command_core.active : NULL;
}

uint32_t AxisCommandPort_FinalPermit(uint32_t now)
{
    uint32_t permit;
    if ((AxisCommand_NeedsDisarm(&command_core, now, board_fault_mask) != 0U) &&
        ((command_core.active_valid != 0U) || (board_output_enabled != 0U)))
        BoardSafety_DisableOutputs();
    permit = AxisCommand_Guard(&command_core, now, board_fault_mask);
    PublishDiagnostics(now);
    return permit;
}

void AxisCommandPort_RejectActive(uint32_t reason)
{
    BoardSafety_DisableOutputs();
    AxisCommand_Revoke(&command_core, reason);
    PublishDiagnostics(HAL_GetTick());
}
