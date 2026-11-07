#ifndef __BOARD_CONFIG_H
#define __BOARD_CONFIG_H

/*
 * PB10 is the active-high common enable for all gate drivers. It is held low
 * during startup/current-offset calibration. appTask prepares the six PWM
 * channels after offset calibration; TIM1 alone enables PB10/MOE after checking
 * an explicit run request, fresh inputs and no latched fault.
 *
 * Supported modes:
 *   BOARD_POWER_STAGE_DISABLED  - no six power PWM outputs
 *   BOARD_POWER_STAGE_GPIO_EN   - use a GPIO gate-enable pin
 *   BOARD_POWER_STAGE_ALWAYS_ON - reserved; rejected by this RTOS integration
 */
#define BOARD_POWER_STAGE_DISABLED          0U
#define BOARD_POWER_STAGE_GPIO_EN           1U
#define BOARD_POWER_STAGE_ALWAYS_ON         2U

#define BOARD_POWER_STAGE_MODE              BOARD_POWER_STAGE_GPIO_EN

#if (BOARD_POWER_STAGE_MODE == BOARD_POWER_STAGE_GPIO_EN)
#define BOARD_GATE_EN_GPIO_CLK_ENABLE()     __HAL_RCC_GPIOB_CLK_ENABLE()
#define BOARD_GATE_EN_GPIO_PORT             GPIOB
#define BOARD_GATE_EN_PIN                   GPIO_PIN_10
#define BOARD_GATE_EN_ACTIVE_LEVEL          GPIO_PIN_SET
#endif

#endif /* __BOARD_CONFIG_H */
