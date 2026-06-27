#include "delay.h"

void delay_approx(void)
{
    volatile uint32_t d = 1200000;
    while (d--);
}

void delay_long(void)
{
    volatile uint32_t d = 4000000;
    while (d--);
}
