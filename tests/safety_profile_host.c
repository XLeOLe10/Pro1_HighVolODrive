#include "main.h"
#include "board_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !AXIS_COMMAND_BENCH_ENABLE || BOARD_POWER_STAGE_MODE != BOARD_POWER_STAGE_DISABLED
#error "Safety behavior test must use bench=1 and disabled power."
#endif

GPIO_TypeDef host_gpio_a, host_gpio_b, host_gpio_e;
TIM_TypeDef host_tim1;
RCC_TypeDef host_rcc;
uint32_t host_primask, host_dsb_count, host_dmb_count, host_gpio_clocks;
static unsigned assertions, gpio_init_calls, status_fault_calls;
#define CHECK(x) do { assertions++; if (!(x)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while (0)
#define POWER_MASK (TIM_CCER_CC1E | TIM_CCER_CC1NE | TIM_CCER_CC2E | \
                    TIM_CCER_CC2NE | TIM_CCER_CC3E | TIM_CCER_CC3NE)

void HAL_GPIO_Init(GPIO_TypeDef *gpio, const GPIO_InitTypeDef *pin)
{
    uint32_t i;
    gpio_init_calls++;
    CHECK(pin->Mode == GPIO_MODE_OUTPUT_PP);
    CHECK(pin->Pull == GPIO_NOPULL);
    for (i=0U; i<16U; ++i) {
        if (pin->Pin & (1U<<i)) {
            MODIFY_REG(gpio->MODER, 3U<<(2U*i), pin->Mode<<(2U*i));
            CLEAR_BIT(gpio->OTYPER, 1U<<i);
            MODIFY_REG(gpio->PUPDR, 3U<<(2U*i), pin->Pull<<(2U*i));
        }
    }
}

void AxisStatus_LatchFault(void)
{
    status_fault_calls++;
    CHECK(board_output_enabled == 0U);
    CHECK((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
    CHECK((TIM1->CCER & POWER_MASK) == 0U);
    CHECK(GPIOB->BSRR == ((uint32_t)GPIO_PIN_10<<16U));
}

static void ResetRegisters(void)
{
    memset(&host_gpio_a,0,sizeof(host_gpio_a));
    memset(&host_gpio_b,0,sizeof(host_gpio_b));
    memset(&host_gpio_e,0,sizeof(host_gpio_e));
    memset(&host_tim1,0,sizeof(host_tim1));
    memset(&host_rcc,0,sizeof(host_rcc));
    board_fault_mask=0U;board_first_fault=0U;board_fault_ipsr=0U;
    board_output_enabled=0U;
    host_primask=0U;host_dsb_count=0U;host_dmb_count=0U;host_gpio_clocks=0U;
    gpio_init_calls=0U;status_fault_calls=0U;
}

static void AssertOutputMode(GPIO_TypeDef *gpio, uint32_t pin)
{
    CHECK(((gpio->MODER>>(pin*2U))&3U) == GPIO_MODE_OUTPUT_PP);
    CHECK(((gpio->OTYPER>>pin)&1U) == 0U);
}

static void TestRefusedEnableAndInit(void)
{
    ResetRegisters();
    RCC->APB2ENR=RCC_APB2ENR_TIM1EN;
    TIM1->CCER=POWER_MASK|TIM_CCER_CC4E;
    TIM1->BDTR=TIM_BDTR_MOE|0x20U;
    GPIOB->MODER=0xffffffffU;GPIOB->OTYPER=0xffffffffU;GPIOB->PUPDR=0xffffffffU;
    GPIOB->ODR=GPIO_PIN_10;
    board_output_enabled=1U;
    BoardSafety_SetOutputsEnabled(1U);
    CHECK(board_output_enabled == 0U);
    CHECK(GPIOB->BSRR == ((uint32_t)GPIO_PIN_10<<16U));
    CHECK(GPIOB->ODR == GPIO_PIN_10); /* No invented BSRR hardware side effect. */
    CHECK((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
    CHECK((TIM1->BDTR & 0x20U) != 0U);
    CHECK((TIM1->CCER & POWER_MASK) == 0U);
    CHECK((TIM1->CCER & TIM_CCER_CC4E) != 0U);
    AssertOutputMode(GPIOB,10U);
    CHECK((host_gpio_clocks & 2U) != 0U);
    CHECK(host_dsb_count != 0U);
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);

    /* Set the readback fixture independently; BSRR is tested as a write request. */
    GPIOA->ODR=0U;GPIOB->ODR=0U;GPIOE->ODR=0U;
    BoardSafety_InitSafe();
    CHECK(gpio_init_calls == 3U);
    CHECK((host_gpio_clocks & 7U) == 7U);
    CHECK(GPIOA->BSRR == ((uint32_t)(GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10)<<16U));
    CHECK(GPIOB->BSRR == ((uint32_t)GPIO_PIN_1<<16U));
    CHECK(GPIOE->BSRR == ((uint32_t)(GPIO_PIN_8|GPIO_PIN_10)<<16U));
    AssertOutputMode(GPIOA,8U);AssertOutputMode(GPIOA,9U);AssertOutputMode(GPIOA,10U);
    AssertOutputMode(GPIOB,1U);AssertOutputMode(GPIOB,10U);
    AssertOutputMode(GPIOE,8U);AssertOutputMode(GPIOE,10U);
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 1U);
}

static void TestReadbackFaults(void)
{
    const uint32_t enables[]={TIM_CCER_CC1E,TIM_CCER_CC1NE,TIM_CCER_CC2E,
        TIM_CCER_CC2NE,TIM_CCER_CC3E,TIM_CCER_CC3NE};
    GPIO_TypeDef *ports[]={GPIOA,GPIOA,GPIOA,GPIOB,GPIOE,GPIOE,GPIOB};
    const uint32_t pins[]={8U,9U,10U,1U,8U,10U,10U};
    unsigned i;
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 1U);
    SET_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);
    CHECK((TIM1->BDTR & TIM_BDTR_MOE) != 0U); /* Guard must not repair first. */
    CLEAR_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    for(i=0U;i<6U;++i) {
        SET_BIT(TIM1->CCER,enables[i]);
        CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);
        CHECK((TIM1->CCER & enables[i]) != 0U);
        CLEAR_BIT(TIM1->CCER,enables[i]);
    }
    for(i=0U;i<7U;++i) {
        uint32_t saved=ports[i]->MODER;
        MODIFY_REG(ports[i]->MODER,3U<<(pins[i]*2U),2U<<(pins[i]*2U));
        CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);
        CHECK(ports[i]->MODER != saved);
        ports[i]->MODER=saved;
        SET_BIT(ports[i]->ODR,1U<<pins[i]);
        CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);
        CHECK((ports[i]->ODR & (1U<<pins[i])) != 0U);
        CLEAR_BIT(ports[i]->ODR,1U<<pins[i]);
    }
    board_output_enabled=1U;
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 0U);
    board_output_enabled=0U;
    CHECK((TIM1->CCER & TIM_CCER_CC4E) != 0U);
    CHECK(BoardSafety_PowerOutputsAreDisabled() == 1U);
}

static void TestFaultOrderAndMaskRestore(void)
{
    uint32_t before=host_dmb_count;
    host_primask=1U;
    TIM1->BDTR|=TIM_BDTR_MOE;TIM1->CCER|=POWER_MASK;
    board_output_enabled=1U;
    BoardSafety_LatchFault(BOARD_FAULT_ASSERT);
    CHECK(host_primask==1U);
    CHECK(host_dmb_count>before);
    CHECK(status_fault_calls==1U);
    CHECK(board_fault_mask==BOARD_FAULT_ASSERT);
    CHECK(board_first_fault==BOARD_FAULT_ASSERT);
    CHECK(board_fault_ipsr==16U);
    CHECK((TIM1->CCER & TIM_CCER_CC4E)!=0U);
    host_primask=0U;
    BoardSafety_LatchFault(BOARD_FAULT_ADC_FRAME);
    CHECK(host_primask==0U);
    CHECK(board_first_fault==BOARD_FAULT_ASSERT);
    CHECK(board_fault_mask==(BOARD_FAULT_ASSERT|BOARD_FAULT_ADC_FRAME));
}

int main(void)
{
    TestRefusedEnableAndInit();
    TestReadbackFaults();
    TestFaultOrderAndMaskRestore();
    printf("PASS: %u safety profile assertions; actual board_safety.c with CPU register stubs only\n",assertions);
    puts("Boundary: BSRR requests are asserted; ODR fixture is explicit, not a model of physical pin transitions.");
    return 0;
}
