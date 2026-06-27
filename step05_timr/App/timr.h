/**
 * 硬件定时器 TIMR0 模块
 *
 * 使用 SWM320 通用定时器产生 1ms 中断，与 SysTick 对比精度。
 * TIMR 是 32 位外设定时器，支持 PWM/输入捕获/编码器等多种模式。
 */

#ifndef __TIMR_H__
#define __TIMR_H__

#include "SWM320.h"

extern volatile uint32_t vTimrTick;

uint32_t get_timr_tick(void);

#endif
