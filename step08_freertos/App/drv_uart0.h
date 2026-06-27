/**
 * UART0 驱动模块 (中断接收 + 查询发送)
 *
 * RX: ISR → 环形缓冲 → main 取出解析
 * TX: fputc 重定向 (查询方式)，printf → 串口
 *
 * PA2=RX, PA3=TX, 115200-8-N-1
 */

#ifndef __DRV_UART0_H__
#define __DRV_UART0_H__

#include "SWM320.h"
#include "loopbuf.h"
#include <stdio.h>

extern loopbuf_t g_uart0_rxbuf;

void UART0_Init(uint32_t baudrate);
int  fputc(int ch, FILE *f);

#endif
