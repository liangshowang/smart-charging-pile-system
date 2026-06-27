/**
 * drvp_uart0.h — UART0 端口驱动接口
 *
 * 接口指针模式:
 *   定义一个函数指针结构体 drvp_uart_t, 全局指针 drvp_uart0 指向其唯一实例。
 *   上层代码只通过 drvp_uart0->xxx() 操作 UART0, 不直接调 SDK 函数。
 *
 * 双回调机制:
 *   RX: ISR 每收一个字节 → rx_callback(byte)
 *       上层在回调里把 byte 写入 RX 环形缓冲
 *
 *   TX: ISR 发现 FIFO 有空位 → tx_callback(&byte)
 *       返回 1: byte 有效, ISR 写入 FIFO
 *       返回 0: 没数据了, ISR 关闭 TX 中断
 *
 *   发送流程:
 *     uart0->send(data, len)
 *       → TX 环形缓冲 ← 应用层写数据
 *       → StartTx(首字节) → 使能 TX 空中断
 *       → ISR 循环:
 *           tx_callback(&byte) → 填 FIFO
 *           环形缓冲空了 → tx_callback 返回 0 → 关 TX 中断
 */

#ifndef __DRVP_UART0_H__
#define __DRVP_UART0_H__

#include <stdint.h>

/* ---- RX 回调: ISR 每收一个字节调用一次 ---- */
typedef void (*uart_rx_callback_t)(uint8_t byte);

/* ---- TX 回调: ISR 索要下一个发送字节
 *     返回 1: byte 有效, 写入 FIFO
 *     返回 0: 没数据可发了, ISR 自行关闭 TX 中断 ---- */
typedef int  (*uart_tx_callback_t)(uint8_t *byte);

/* ---- 驱动接口结构体 (虚函数表) ---- */
typedef struct {
    void (*Init)(uint32_t baudrate);
    void (*Open)(void);
    void (*Close)(void);

    /* 阻塞发送 (保留, 供 printf 单字节直发等场景) */
    void (*WriteByte)(uint8_t byte);
    int  (*IsTXBusy)(void);

    /* RX 中断回调 */
    void (*SetRxCallback)(uart_rx_callback_t cb);

    /* TX 中断回调 + 启停 */
    void (*SetTxCallback)(uart_tx_callback_t cb);
    void (*StartTx)(uint8_t first_byte);  /* 写首字节 + 使能 TX 空中断 */
    void (*StopTx)(void);                 /* 关闭 TX 空中断 */

    /* NVIC 开关 */
    void (*EnableIRQ)(int enable);
} drvp_uart_t, *drvp_uart_pt;

/* ---- 全局接口指针 (由 drvp_uart0.c 定义) ---- */
extern drvp_uart_pt drvp_uart0;

#endif /* __DRVP_UART0_H__ */
