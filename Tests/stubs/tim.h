#ifndef TEST_TIM_H
#define TEST_TIM_H
#include <stdint.h>
typedef struct { uint32_t arr; uint32_t compare[3]; } TIM_HandleTypeDef;
extern TIM_HandleTypeDef htim1;
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 1U
#define TIM_CHANNEL_3 2U
#define __HAL_TIM_GET_AUTORELOAD(timer) ((timer)->arr)
#define __HAL_TIM_SET_COMPARE(timer, channel, value) ((timer)->compare[channel] = (value))
#endif
