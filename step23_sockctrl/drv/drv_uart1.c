/**
 * drv_uart1.c — UART1 驱动层实现 (双环形缓冲)
 *
 * 用于 4G 模块 (AIR724) 通信的 UART1。
 * 与 drv_uart0.c 架构完全一致:
 *   - 管理 TX + RX 两个环形缓冲
 *   - 向端口层注册 RX/TX 回调
 *   - 提供 uart1 接口指针
 *
 * TX 流程:
 *   uart1->send(data, len)
 *     → 写入 TX loopbuf
 *     → 如果 TX 空闲 → 取首字节 → drvp_uart1->StartTx(first_byte)
 *       → 使能 TX 空中断 → ISR 通过 tx_callback 继续索要字节
 *     → 如果 TX 忙 → 数据留在环形缓冲, ISR 会自动取走
 *
 * 无直接 SDK 调用 — 所有硬件操作通过 drvp_uart1 接口指针。
 */

#include "drv_uart1.h"
#include "drv_loop.h"

/* ================================================================
 * 内部数据
 * ================================================================ */
static loopbuf_pt g_uart1_rx = NULL;     /* RX 环形缓冲 */
static loopbuf_pt g_uart1_tx = NULL;     /* TX 环形缓冲 */
static volatile int tx1_running = 0;     /* TX ISR 运行标志 */

/* ================================================================
 * uart1_rx_handler — RX 字节回调 (在 drvp_uart1 ISR 中调用)
 * ================================================================ */
static void uart1_rx_handler(uint8_t byte)
{
    loop->write(g_uart1_rx, byte);
}

/* ================================================================
 * uart1_tx_handler — TX 字节回调 (在 drvp_uart1 ISR 中调用)
 *
 *   返回 1: *byte 有效, ISR 写入 TX FIFO
 *   返回 0: 环形缓冲空, tx1_running = 0, ISR 关闭 TX 中断
 * ================================================================ */
static int uart1_tx_handler(uint8_t *byte)
{
    if (loop->read(g_uart1_tx, byte, 1) > 0)
        return 1;

    tx1_running = 0;
    return 0;
}

/* ================================================================
 * kick_tx1 — 如果 TX 空闲, 从环形缓冲取首字节启动发送
 * ================================================================ */
static void kick_tx1(void)
{
    if (!tx1_running) {
        uint8_t first;
        if (loop->read(g_uart1_tx, &first, 1) > 0) {
            tx1_running = 1;
            drvp_uart1->StartTx(first);
        }
    }
}

/* ================================================================
 * do_init — 初始化 UART1 驱动
 * ================================================================ */
static void do_init(uint32_t baudrate)
{
    g_uart1_rx = loop->create();
    g_uart1_tx = loop->create();
    if (g_uart1_rx == NULL || g_uart1_tx == NULL) {
        while (1);  /* 内存分配失败 */
    }

    tx1_running = 0;

    drvp_uart1->SetRxCallback(uart1_rx_handler);
    drvp_uart1->SetTxCallback(uart1_tx_handler);

    drvp_uart1->Init(baudrate);
    drvp_uart1->Open();
}

/* ================================================================
 * do_read_rx_buf — 从 RX 环形缓冲读数据
 * ================================================================ */
static int do_read_rx_buf(uint8_t *buf, int len)
{
    return loop->read(g_uart1_rx, buf, (unsigned char)len);
}

/* ================================================================
 * do_send — 非阻塞发送多字节
 * ================================================================ */
static void do_send(const uint8_t *data, int len)
{
    int i;
    for (i = 0; i < len; i++)
        loop->write(g_uart1_tx, data[i]);
    kick_tx1();
}

/* ================================================================
 * do_send_byte — 非阻塞发送单字节
 * ================================================================ */
static void do_send_byte(uint8_t byte)
{
    loop->write(g_uart1_tx, byte);
    kick_tx1();
}

/* ================================================================
 * do_clear_rx_buf — 清空 RX 环形缓冲
 * ================================================================ */
static void do_clear_rx_buf(void)
{
    loop->reset(g_uart1_rx);
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_uart1_t m_uart1 = {
    do_init,
    do_read_rx_buf,
    do_send,
    do_send_byte,
    do_clear_rx_buf,
};

drv_uart1_pt uart1 = &m_uart1;
