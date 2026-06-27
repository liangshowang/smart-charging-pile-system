/**
 * drvp_uart0.h — UART0 端口驱动接口
 *
 * 接口指针模式:
 *   定义一个函数指针结构体 drvp_uart_t, 全局指针 drvp_uart0 指向其唯一实例。
 *   上层代码只通过 drvp_uart0->xxx() 操作 UART0, 不直接调 SDK 函数。
 *
 * 与 GPIO 端口驱动的区别:
 *   UART 有数据流——中断里收字节后通过回调通知上层。
 *   GPIO 是电平/边沿触发——中断里通过 callback(arg) 通知。
 *
 * 回调机制:
 *   SetRxCallback(fn) — 注册一个函数指针
 *   ISR 每收到一个字节, 调用 fn(byte)
 *   上层 (drv_uart0) 在回调里把 byte 写入环形缓冲
 */

#ifndef __DRVP_UART0_H__
#define __DRVP_UART0_H__

#include <stdint.h>

/* ---- RX 回调: ISR 每收一个字节调用一次 ---- */
typedef void (*uart_rx_callback_t)(uint8_t byte);

/* ---- 驱动接口结构体 (虚函数表) ---- */
typedef struct {
    void (*Init)(uint32_t baudrate);
    void (*Open)(void);
    void (*Close)(void);
    void (*WriteByte)(uint8_t byte);
    int  (*IsTXBusy)(void);
    void (*SetRxCallback)(uart_rx_callback_t cb);
    void (*EnableIRQ)(int enable);
} drvp_uart_t, *drvp_uart_pt;

/* ---- 全局接口指针 (由 drvp_uart0.c 定义) ---- */
extern drvp_uart_pt drvp_uart0;

#endif /* __DRVP_UART0_H__ */
