/**
 * drv_uart0.h — UART0 驱动层接口
 *
 * 提供平台无关的 UART 抽象: 双环形缓冲 (TX+RX) + 接口指针。
 * 硬件操作通过 drvp_uart0 接口指针完成。
 *
 * 使用:
 *   uart0->init(115200);
 *   uart0->send((uint8_t*)"hello\r\n", 7);    // 非阻塞发送
 *   int n = uart0->read_rx_buf(buf, 128);     // 读接收数据
 *   printf("...");   // fputc → uart0->send_byte()
 *
 * 数据流:
 *   TX: send → TX loopbuf → kick_tx → ISR (drvp_uart0)
 *         → tx_callback → 读 TX loopbuf → 填 TX FIFO
 *
 *   RX: 硬件 RX FIFO → ISR (drvp_uart0) → rx_callback
 *         → 写 RX loopbuf → read_rx_buf 取出
 */

#ifndef __DRV_UART0_H__
#define __DRV_UART0_H__

#include "SWM320.h"
#include "drv_loop.h"
#include "drvp_uart0.h"
#include <stdio.h>

/* ---- 驱动接口 (虚函数表) ---- */
typedef struct {
    void (*init)(uint32_t baudrate);
    int  (*read_rx_buf)(uint8_t *buf, int len);
    void (*send)(const uint8_t *data, int len);
    void (*send_byte)(uint8_t byte);
    void (*clear_rx_buf)(void);
} drv_uart_t, *drv_uart_pt;

/* ---- 全局接口指针 ---- */
extern drv_uart_pt uart0;

/* ---- printf 重定向 ---- */
int fputc(int ch, FILE *f);

#endif
