/**
 * UART0 驱动模块 (中断接收 + 查询发送)
 *
 * RX: ISR → 环形缓冲(drv_loop) → cmd_task 取出处理
 * TX: fputc 重定向 (查询方式), printf → 串口
 *
 * PA2=RX, PA3=TX, 115200-8-N-1
 */

#ifndef __DRV_UART0_H__
#define __DRV_UART0_H__

#include "SWM320.h"
#include "drv_loop.h"
#include <stdio.h>

/* UART0 专用的环形缓冲实例 (在 UART0_Init 中创建) */
extern loopbuf_pt g_uart0_loop;

void UART0_Init(uint32_t baudrate);
int  fputc(int ch, FILE *f);

#endif
