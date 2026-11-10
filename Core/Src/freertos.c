/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_runtime.h"
#include "app_policy.h"
#include "board_safety.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for appTask */
osThreadId_t appTaskHandle;
const osThreadAttr_t appTask_attributes = {
  .name = "appTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartAppTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 2 */
void vApplicationIdleHook(void)
{
  static uint32_t last_probe_tick;
  uint32_t now;
  uint32_t free_bytes;

  /* appTask is created before scheduler start and is never deleted/replaced.
   * NULL would measure the idle task itself, so never pass it to this API. */
  if (appTaskHandle == NULL)
  {
    return;
  }
#if AXIS_COMMAND_BENCH_ENABLE
  if (app_stack_probe_enabled == 0U)
  {
    return;
  }
#endif
  now = HAL_GetTick();
  if ((uint32_t)(now - last_probe_tick) < APP_DIAGNOSTIC_PERIOD_MS)
  {
    return;
  }

  /* Kernel V10.3.1 scans the task's stack fill without suspending scheduling
   * or masking interrupts. appTask and TIM1 can preempt this idle-priority
   * work. No wait, allocation, output, or blocking API is allowed here.
   * Stack use may change while preempted: this is delayed diagnostic evidence,
   * not overflow protection or proof of worst-case stack safety. */
  free_bytes = (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)appTaskHandle)
             * (uint32_t)sizeof(StackType_t);
  if (free_bytes < app_stack_min_free_bytes)
  {
    app_stack_min_free_bytes = free_bytes;
  }
  /* Rate-limit from completion: never catch up with back-to-back scans if
   * higher-priority work starved idle. A stale timestamp remains observable. */
  last_probe_tick = HAL_GetTick();
  app_stack_probe_last_tick_ms = last_probe_tick;
  app_stack_probe_count++;
}
/* USER CODE END 2 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  BoardSafety_Fatal(BOARD_FAULT_STACK);
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
  BoardSafety_Fatal(BOARD_FAULT_MALLOC);
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* Also catches a failed osKernelInitialize without modifying Cube's call. */
  if (osKernelGetState() != osKernelReady)
  {
    BoardSafety_Fatal(BOARD_FAULT_KERNEL);
  }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of appTask */
  appTaskHandle = osThreadNew(StartAppTask, NULL, &appTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  if (appTaskHandle == NULL)
  {
    BoardSafety_Fatal(BOARD_FAULT_TASK_CREATE);
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartAppTask */
/**
  * @brief  Function implementing the appTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartAppTask */
void StartAppTask(void *argument)
{
  /* USER CODE BEGIN StartAppTask */
  uint32_t deadline;
  uint32_t now;
  uint32_t started;
  osStatus_t status;
  (void)argument;
  AppRuntime_TaskStart();
  deadline = osKernelGetTickCount() + APP_PERIOD_TICKS;
  for (;;)
  {
    started = DWT->CYCCNT;
    AppRuntime_BackgroundStep();
    app_task_last_cycles = (uint32_t)(DWT->CYCCNT - started);
    if (app_task_last_cycles > app_task_max_cycles)
    {
      app_task_max_cycles = app_task_last_cycles;
    }
    now = osKernelGetTickCount();
    if (APP_TIME_REACHED(now, deadline))
    {
      app_task_deadline_misses++;
    }
    deadline = APP_NEXT_WAKE(now, deadline);
    status = osDelayUntil(deadline);
    if (status == osErrorParameter)
    {
      /* Tick advanced between the check and delay. Yield one tick, never spin. */
      app_task_deadline_misses++;
      if (osDelay(APP_PERIOD_TICKS) != osOK)
      {
        BoardSafety_Fatal(BOARD_FAULT_SCHEDULER);
      }
      deadline = osKernelGetTickCount();
    }
    else if (status != osOK)
    {
      BoardSafety_Fatal(BOARD_FAULT_SCHEDULER);
    }
    deadline += APP_PERIOD_TICKS;
  }
  /* USER CODE END StartAppTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

