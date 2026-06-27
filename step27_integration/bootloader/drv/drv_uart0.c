/**
 * drv_uart0.c — UART0 驱动层实现 (双环形缓冲)
 *
 * 职责:
 *   - 管理 TX + RX 两个环形缓冲
 *   - 向端口层注册 RX/TX 回调
 *   - 提供 uart0 接口指针
 *
 * 内部细节:
 *   g_uart0_rx — RX 环形缓冲 (硬件 RX FIFO → 应用层)
 *   g_uart0_tx — TX 环形缓冲 (应用层 → 硬件 TX FIFO)
 *   tx_running — 标记 TX ISR 是否正在运行
 *
 * TX 流程:
 *   uart0->send(data, len)
 *     → 写入 TX loopbuf
 *     → 如果 TX 空闲 → 取首字节 → drvp_uart0->StartTx(first_byte)
 *       → 使能 TX 空中断 → ISR 通过 tx_callback 继续索要字节
 *     → 如果 TX 忙 → 数据留在环形缓冲, ISR 会自动取走
 *
 * 无直接 SDK 调用 — 所有硬件操作通过 drvp_uart0 接口指针。
 */

#include "drv_uart0.h"
#include "drv_loop.h"

/* ================================================================
 * 内部数据
 * ================================================================ */
static loopbuf_pt g_uart0_rx = NULL;     /* RX 环形缓冲 */
static loopbuf_pt g_uart0_tx = NULL;     /* TX 环形缓冲 */

/*
 * tx_running: 标记 TX ISR 是否在运行
 *   1 = TX 空中断已使能, ISR 正在从 TX 环形缓冲取数据发送
 *   0 = TX 空闲, 有新数据时需要手动 kick
 *
 * 在中断内外都可能被修改, 用 volatile。
 */
static volatile int tx_running = 0;

/* ================================================================
 * uart0_rx_handler — RX 字节回调 (在 drvp_uart0 ISR 中调用)
 *
 * 每收到一个字节, 写入 RX 环形缓冲。
 * loop->write 是 ISR 安全的。
 * ================================================================ */
static void uart0_rx_handler(uint8_t byte)
{
    loop->write(g_uart0_rx, byte);
}

/* ================================================================
 * uart0_tx_handler — TX 字节回调 (在 drvp_uart0 ISR 中调用)
 *
 * ISR 索要下一个待发送的字节。
 * 从 TX 环形缓冲读一字节:
 *   返回 1: *byte 有效, ISR 写入 TX FIFO
 *   返回 0: 环形缓冲空, tx_running = 0, ISR 关闭 TX 中断
 * ================================================================ */
static int uart0_tx_handler(uint8_t *byte)
{
    if (loop->read(g_uart0_tx, byte, 1) > 0)
        return 1;

    /* 没数据了 — 标记 TX 停止 */
    tx_running = 0;
    return 0;
}

/* ================================================================
 * kick_tx — 如果 TX 空闲, 从环形缓冲取首字节启动发送
 *
 * 必须在 TX 缓冲有数据后才能调用。
 * ================================================================ */
static void kick_tx(void)
{
    if (!tx_running) {
        uint8_t first;
        if (loop->read(g_uart0_tx, &first, 1) > 0) {
            tx_running = 1;
            drvp_uart0->StartTx(first);
        }
    }
}

/* ================================================================
 * do_init — 初始化 UART0 驱动
 *
 * 流程:
 *   1. 创建 TX + RX 两个环形缓冲
 *   2. 向端口层注册 RX/TX 回调
 *   3. 委托端口层完成硬件初始化
 *   4. 开启 UART 外设
 * ================================================================ */
static void do_init(uint32_t baudrate)
{
    /* 创建环形缓冲 (必须在硬件初始化前, 因为中断可能马上来) */
    g_uart0_rx = loop->create();
    g_uart0_tx = loop->create();
    if (g_uart0_rx == NULL || g_uart0_tx == NULL) {
        while (1);  /* 内存分配失败 — 不应发生 */
    }

    tx_running = 0;

    /* 注册回调 */
    drvp_uart0->SetRxCallback(uart0_rx_handler);
    drvp_uart0->SetTxCallback(uart0_tx_handler);

    /* 硬件初始化 + 开启 */
    drvp_uart0->Init(baudrate);
    drvp_uart0->Open();
}

/* ================================================================
 * do_read_rx_buf — 从 RX 环形缓冲读数据
 *
 * 返回实际读到的字节数。
 * ================================================================ */
static int do_read_rx_buf(uint8_t *buf, int len)
{
    return loop->read(g_uart0_rx, buf, (unsigned char)len);
}

/* ================================================================
 * do_send — 非阻塞发送多字节
 *
 * 写入 TX 环形缓冲后尝试启动发送。
 * 如果 ISR 正在运行, 数据会被自动取走。
 * ================================================================ */
static void do_send(const uint8_t *data, int len)
{
    int i;

    for (i = 0; i < len; i++)
        loop->write(g_uart0_tx, data[i]);

    kick_tx();
}

/* ================================================================
 * do_send_byte — 非阻塞发送单字节
 *
 * 供 fputc 调用, 实现非阻塞 printf。
 * ================================================================ */
static void do_send_byte(uint8_t byte)
{
    loop->write(g_uart0_tx, byte);
    kick_tx();
}

/* ================================================================
 * do_clear_rx_buf — 清空 RX 环形缓冲
 * ================================================================ */
static void do_clear_rx_buf(void)
{
    loop->reset(g_uart0_rx);
}

/* ================================================================
 * fputc — printf 重定向 (非阻塞, 走 TX 环形缓冲)
 *
 * 不再阻塞等 TX 完成 — 字节写入环形缓冲, ISR 后台发送。
 * ================================================================ */
/* fputc 由 main.c 提供 (bare-metal 轮询版本), drv_uart0 不重复定义 */
#if 0
int fputc(int ch, FILE *f)
{
    (void)f;
    do_send_byte((uint8_t)ch);
    return ch;
}
#endif

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_uart_t m_uart0 = {
    do_init,
    do_read_rx_buf,
    do_send,
    do_send_byte,
    do_clear_rx_buf,
};

drv_uart_pt uart0 = &m_uart0;
