#include "main.h"
#include "board_config.h"
#include "board_safety.h"

#if (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_GPIO_EN) && \
    (BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED)
#error "RTOS bring-up requires a gate EN; ALWAYS_ON needs a separate startup design."
#endif

#define POWER_CHANNEL_ENABLES (TIM_CCER_CC1E | TIM_CCER_CC1NE | \
                               TIM_CCER_CC2E | TIM_CCER_CC2NE | \
                               TIM_CCER_CC3E | TIM_CCER_CC3NE)

volatile uint32_t board_fault_mask = 0U;
volatile uint32_t board_first_fault = 0U;
volatile uint32_t board_fault_ipsr = 0U;
volatile uint8_t board_output_enabled = 0U;

/* Direct register access also works before MX_GPIO_Init / the scheduler.
 * No waits, allocation, formatted output or RTOS services on a fault path. */
static void GateInactive(void)
{
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
    uint32_t shift = POSITION_VAL(BOARD_GATE_EN_PIN) * 2U;
    BOARD_GATE_EN_GPIO_CLK_ENABLE();
    BOARD_GATE_EN_GPIO_PORT->BSRR =
        (BOARD_GATE_EN_ACTIVE_LEVEL == GPIO_PIN_SET) ?
        ((uint32_t)BOARD_GATE_EN_PIN << 16U) : BOARD_GATE_EN_PIN;
    CLEAR_BIT(BOARD_GATE_EN_GPIO_PORT->OTYPER, BOARD_GATE_EN_PIN);
    MODIFY_REG(BOARD_GATE_EN_GPIO_PORT->PUPDR, 3UL << shift, 0U);
    MODIFY_REG(BOARD_GATE_EN_GPIO_PORT->MODER, 3UL << shift, 1UL << shift);
#endif
}

void BoardSafety_DisableOutputs(void)
{
    GateInactive();
    if ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U)
    {
        /* The conditional HAL MOE macro would not clear MOE with CC4E set. */
        CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE);
    }
    board_output_enabled = 0U;
    __DSB();
}

void BoardSafety_InitSafe(void)
{
    GPIO_InitTypeDef pin = {0};
    BoardSafety_DisableOutputs();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIOA->BSRR = ((uint32_t)(GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10) << 16U);
    GPIOB->BSRR = ((uint32_t)GPIO_PIN_1 << 16U);
    GPIOE->BSRR = ((uint32_t)(GPIO_PIN_8 | GPIO_PIN_10) << 16U);
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_8 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &pin);
}

void BoardSafety_SetOutputsEnabled(uint8_t enable)
{
#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
    if ((enable != 0U) && (board_fault_mask == 0U))
    {
        SET_BIT(TIM1->BDTR, TIM_BDTR_MOE);
        BOARD_GATE_EN_GPIO_PORT->BSRR =
            (BOARD_GATE_EN_ACTIVE_LEVEL == GPIO_PIN_SET) ?
            BOARD_GATE_EN_PIN : ((uint32_t)BOARD_GATE_EN_PIN << 16U);
        board_output_enabled = 1U;
        __DSB();
        return;
    }
#else
    (void)enable;
#endif
    BoardSafety_DisableOutputs();
}

void BoardSafety_LatchFault(uint32_t reason)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    BoardSafety_DisableOutputs();
    if ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U)
    {
        CLEAR_BIT(TIM1->CCER, POWER_CHANNEL_ENABLES);
    }
    if (board_first_fault == 0U)
    {
        board_first_fault = reason;
        board_fault_ipsr = __get_IPSR();
    }
    board_fault_mask |= reason;
    __DMB();
    __set_PRIMASK(saved);
}

void BoardSafety_Fatal(uint32_t reason)
{
    __disable_irq();
    BoardSafety_LatchFault(reason);
    for (;;)
    {
        /* Do not return into partly-created tasks or interrupted startup. */
        __NOP();
    }
}
