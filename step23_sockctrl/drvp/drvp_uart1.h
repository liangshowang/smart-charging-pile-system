/**
 * drvp_uart1.h — UART1 端口驱动接口 (4G 模块专用)
 *
 * 接口与 drvp_uart0 完全一致, 区别:
 *   - 使用 UART1 外设
 *   - 引脚 PA4=RX, PA5=TX (V5_4)
 *   - 中断号 UART1_IRQn = 40
 *
 * 双回调机制:
 *   RX: ISR 每收一个字节 → rx_callback(byte)
 *       上层在回调里把 byte 写入 RX 环形缓冲
 *
 *   TX: ISR 发现 FIFO 有空位 → tx_callback(&byte)
 *       返回 1: byte 有效, ISR 写入 FIFO
 *       返回 0: 没数据了, ISR 关闭 TX 中断
 */

#ifndef __DRVP_UART1_H__
#define __DRVP_UART1_H__

#include <stdint.h>

/* ---- RX 回调: ISR 每收一个字节调用一次 ---- */
typedef void (*uart1_rx_callback_t)(uint8_t byte);

/* ---- TX 回调: ISR 索要下一个发送字节
 *     返回 1: byte 有效, 写入 FIFO
 *     返回 0: 没数据可发了, ISR 自行关闭 TX 中断 ---- */
typedef int  (*uart1_tx_callback_t)(uint8_t *byte);

/* ---- 驱动接口结构体 (虚函数表) ---- */
typedef struct {
    void (*Init)(uint32_t baudrate);
    void (*Open)(void);
    void (*Close)(void);

    /* 阻塞发送 (保留, 供调试等场景) */
    void (*WriteByte)(uint8_t byte);
    int  (*IsTXBusy)(void);

    /* RX 中断回调 */
    void (*SetRxCallback)(uart1_rx_callback_t cb);

    /* TX 中断回调 + 启停 */
    void (*SetTxCallback)(uart1_tx_callback_t cb);
    void (*StartTx)(uint8_t first_byte);  /* 写首字节 + 使能 TX 空中断 */
    void (*StopTx)(void);                 /* 关闭 TX 空中断 */

    /* NVIC 开关 */
    void (*EnableIRQ)(int enable);
} drvp_uart1_t, *drvp_uart1_pt;

/* ---- 全局接口指针 (由 drvp_uart1.c 定义) ---- */
extern drvp_uart1_pt drvp_uart1;

#endif /* __DRVP_UART1_H__ */
