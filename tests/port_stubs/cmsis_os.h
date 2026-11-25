#ifndef COMMAND_PORT_HOST_CMSIS_OS_H
#define COMMAND_PORT_HOST_CMSIS_OS_H
typedef void *osThreadId_t;
typedef enum { osKernelInactive = 0, osKernelRunning = 2 } osKernelState_t;
osKernelState_t osKernelGetState(void);
osThreadId_t osThreadGetId(void);
#endif
