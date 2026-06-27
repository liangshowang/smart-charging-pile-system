/**
 * UART0 调试串口模块
 *
 * PA2=RX, PA3=TX, 115200-8-N-1
 * fputc 重定向 → printf 输出到串口
 */

#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <stdio.h>

void SerialInit(void);

/* printf 重定向 (需 Keil 勾选 Use MicroLIB) */
int fputc(int ch, FILE *f);

#endif
