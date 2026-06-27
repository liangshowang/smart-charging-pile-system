/**
 * SysTick 系统滴答模块
 *
 * 提供 1ms 精度的全局计数器，替代阻塞延时。
 * 后续 FreeRTOS 的 vTaskDelay 底层用的就是这个。
 *
 * 用法:
 *   SysTick_Config(SystemCoreClock / 1000);  // 启动 1ms 中断
 *   uint32_t t = get_systick();              // 读当前毫秒计数
 */

#ifndef __SYSTICK_H__
#define __SYSTICK_H__

#include <stdint.h>

extern volatile uint32_t vSysTick;

uint32_t get_systick(void);

#endif
