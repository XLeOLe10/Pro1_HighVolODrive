#ifndef HOST_SAFETY_MAIN_H
#define HOST_SAFETY_MAIN_H
#include <stdint.h>

/* CPU register storage only. Writing BSRR deliberately does NOT mutate ODR.
 * Assertions distinguish a reset write request from physical pin readback. */
typedef struct {
    volatile uint32_t MODER, OTYPER, PUPDR, ODR, BSRR;
} GPIO_TypeDef;
typedef struct { volatile uint32_t BDTR, CCER; } TIM_TypeDef;
typedef struct { volatile uint32_t APB2ENR; } RCC_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
extern GPIO_TypeDef host_gpio_a, host_gpio_b, host_gpio_e;
extern TIM_TypeDef host_tim1;
extern RCC_TypeDef host_rcc;
extern uint32_t host_primask, host_dsb_count, host_dmb_count, host_gpio_clocks;
#define GPIOA (&host_gpio_a)
#define GPIOB (&host_gpio_b)
#define GPIOE (&host_gpio_e)
#define TIM1 (&host_tim1)
#define RCC (&host_rcc)
#define GPIO_PIN_1  (1U << 1U)
#define GPIO_PIN_8  (1U << 8U)
#define GPIO_PIN_9  (1U << 9U)
#define GPIO_PIN_10 (1U << 10U)
#define GPIO_PIN_SET 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define TIM_BDTR_MOE (1U << 15U)
#define TIM_CCER_CC1E (1U << 0U)
#define TIM_CCER_CC1NE (1U << 2U)
#define TIM_CCER_CC2E (1U << 4U)
#define TIM_CCER_CC2NE (1U << 6U)
#define TIM_CCER_CC3E (1U << 8U)
#define TIM_CCER_CC3NE (1U << 10U)
#define TIM_CCER_CC4E (1U << 12U)
#define RCC_APB2ENR_TIM1EN 1U
#define POSITION_VAL(x) ((uint32_t)__builtin_ctz((unsigned)(x)))
#define CLEAR_BIT(reg, mask) ((reg) &= ~(mask))
#define SET_BIT(reg, mask) ((reg) |= (mask))
#define MODIFY_REG(reg, clear, set) ((reg) = (((reg) & ~(clear)) | (set)))
#define __HAL_RCC_GPIOA_CLK_ENABLE() (host_gpio_clocks |= 1U)
#define __HAL_RCC_GPIOB_CLK_ENABLE() (host_gpio_clocks |= 2U)
#define __HAL_RCC_GPIOE_CLK_ENABLE() (host_gpio_clocks |= 4U)
#define __get_PRIMASK() (host_primask)
#define __disable_irq() (host_primask = 1U)
#define __set_PRIMASK(x) (host_primask = (x))
#define __get_IPSR() 16U
#define __DSB() (host_dsb_count++)
#define __DMB() (host_dmb_count++)
#define __NOP() ((void)0)
void HAL_GPIO_Init(GPIO_TypeDef *gpio, const GPIO_InitTypeDef *pin);
#endif
