#include "SWM320.h"
#include "timr.h"

volatile uint32_t vTimrTick = 0;

void TIMR0_Handler(void)
{
    TIMR_INTClr(TIMR0);     /* 清中断标志（必须！SysTick 自动清，TIMR 要手动） */
    vTimrTick++;
}

uint32_t get_timr_tick(void)
{
    return vTimrTick;
}
