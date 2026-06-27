/**
 * drv_uart0.h — UART0 驱动层接口
 *
 * 平台无关的 UART 抽象: 环形缓冲管理 + fputc 重定向。
 * 硬件操作通过 drvp_uart0 接口指针完成。
 *
 * 架构:
 *   cmd_task → loop->read(g_uart0_loop)    ← 驱动层提供的环形缓冲
 *   printf   → fputc → drvp_uart0->WriteByte()  ← 硬件发送
 *   ISR (drvp_uart0) → rx_callback → loop->write() ← 硬件→缓冲
 *
 * PA2=RX, PA3=TX, 115200-8-N-1
 */

#ifndef __DRV_UART0_H__
#define __DRV_UART0_H__

#include "SWM320.h"
#include "drv_loop.h"
#include "drvp_uart0.h"
#include <stdio.h>

/* UART0 专用的环形缓冲实例 (在 UART0_Init 中创建) */
extern loopbuf_pt g_uart0_loop;

void UART0_Init(uint32_t baudrate);
int  fputc(int ch, FILE *f);

#endif
