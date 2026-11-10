#ifndef COMMAND_PORT_HOST_MAIN_H
#define COMMAND_PORT_HOST_MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t CYCCNT; } HostDwt;
extern HostDwt host_dwt;
#define DWT (&host_dwt)
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
#endif
