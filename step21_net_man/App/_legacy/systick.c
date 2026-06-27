#include "systick.h"

volatile uint32_t vSysTick = 0;

void SysTick_Handler(void)
{
    vSysTick++;
}

uint32_t get_systick(void)
{
    return vSysTick;
}
