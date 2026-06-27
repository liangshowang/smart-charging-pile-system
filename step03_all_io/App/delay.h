/**
 * 简易阻塞延时
 *
 * 注意: 仅用于裸机阶段调试，上了 FreeRTOS 后用 vTaskDelay 替换。
 */

#ifndef __DELAY_H__
#define __DELAY_H__

#include <stdint.h>

void delay_approx(void);    /* ~0.3s */
void delay_long(void);       /* ~1.0s */

#endif
