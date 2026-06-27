/**
 * drv_uart0.c — UART0 驱动层实现
 *
 * 职责:
 *   - 管理 UART0 的环形缓冲 (g_uart0_loop)
 *   - 注册 RX 回调给端口层, 在回调中把字节写入环形缓冲
 *   - 提供 fputc 重定向 (查询发送)
 *
 * 不直接调 SDK 函数 — 所有硬件操作通过 drvp_uart0 接口指针。
 *
 * 回调链路:
 *   硬件 RX FIFO
 *     → drvp_uart0 ISR (读 FIFO)
 *       → rx_callback (本文件注册的函数)
 *         → loop->write(g_uart0_loop, byte)
 *           → cmd_line_work → loop->read() 取出
 */

#include "drv_uart0.h"
#include "drv_loop.h"

/* UART0 接收环形缓冲 (在 UART0_Init 中分配) */
loopbuf_pt g_uart0_loop = NULL;

/* ================================================================
 * uart0_rx_handler — RX 字节回调
 *
 * 由 drvp_uart0 ISR 调用, 运行在中断上下文。
 * 把收到的字节写入环形缓冲。
 * loop->write 是 ISR 安全的 (只操作 head 指针, 不阻塞)。
 * ================================================================ */
static void uart0_rx_handler(uint8_t byte)
{
    loop->write(g_uart0_loop, byte);
}

/* ================================================================
 * UART0_Init — 初始化 UART0 驱动
 *
 * 流程:
 *   1. 创建环形缓冲 (1024 字节)
 *   2. 向端口层注册 RX 回调
 *   3. 委托端口层完成硬件初始化
 *   4. 开启 UART 外设
 *
 * 注意: 环形缓冲必须在硬件初始化之前创建,
 *       因为 drvp_uart0->Init 之后中断就可能触发。
 * ================================================================ */
void UART0_Init(uint32_t baudrate)
{
    /* 1. 创建环形缓冲 */
    g_uart0_loop = loop->create();
    if (g_uart0_loop == NULL) {
        /* 内存分配失败 — 不应发生 (90KB heap 足够) */
        while (1);
    }

    /* 2. 注册 RX 回调 (ISR 中每收到一个字节调用) */
    drvp_uart0->SetRxCallback(uart0_rx_handler);

    /* 3. 端口层硬件初始化 (引脚复用、波特率、FIFO、中断) */
    drvp_uart0->Init(baudrate);

    /* 4. 开启 UART 外设 */
    drvp_uart0->Open();
}

/* ================================================================
 * fputc — printf 重定向到 UART0
 *
 * 查询发送: 写数据 → 等 TX 完成 → 返回。
 * 非中断方式, 发送大块数据会短暂阻塞。
 * ================================================================ */
int fputc(int ch, FILE *f)
{
    (void)f;  /* 未使用 */
    drvp_uart0->WriteByte((uint8_t)ch);
    return ch;
}
