/**
 * drvp_uart0.c — UART0 端口驱动实现 (SWM320)
 *
 * 架构:
 *   drvp_uart0 (全局指针) → m_drvp_uart0 (静态实例)
 *     → { Init, Open, Close, WriteByte, IsTXBusy, SetRxCallback, EnableIRQ }
 *       → SDK 函数 (UART_Init, UART_ReadByte, ...)
 *
 * ISR 归端口层:
 *   UART0_Handler 直接访问 UART0 寄存器。
 *   每收到一个字节, 通过回调函数通知上层 (drv_uart0)。
 *
 *   回调链路:
 *     UART0 RX FIFO → ISR → rx_callback(byte)
 *       → drv_uart0: loop->write(g_uart0_loop, byte)
 *
 * 引脚: PA2=RX, PA3=TX
 */

#include "SWM320.h"
#include "drvp_uart0.h"
#include <stddef.h>

/* ================================================================
 * 模块级变量
 * ================================================================ */

/* RX 回调函数指针 (由上层 drv_uart0 注册) */
static uart_rx_callback_t rx_callback = NULL;

/* ================================================================
 * do_init — 硬件初始化
 *
 *   1. 配置引脚复用 (PA2→UART0_RX, PA3→UART0_TX)
 *   2. 配置 UART 参数 (波特率/数据位/校验/停止位)
 *   3. 配置 RX FIFO 阈值中断 + 超时中断
 *
 * 注意: 不在此处使能 NVIC, 由 do_open 统一处理
 * ================================================================ */
static void do_init(uint32_t baudrate)
{
    UART_InitStructure u;

    /* 引脚功能选择 */
    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);   /* PA2: RX, 上拉输入 */
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);   /* PA3: TX, 推挽输出 */

    /* UART 参数配置 */
    u.Baudrate       = baudrate;
    u.DataBits       = UART_DATA_8BIT;
    u.Parity         = UART_PARITY_NONE;
    u.StopBits       = UART_STOP_1BIT;

    /*
     * RX 中断配置:
     *   RXThreshold     = 7 — FIFO 中超过 7 字节触发 RX_THR 中断
     *   RXThresholdIEn  = 1 — 使能阈值中断
     *
     *   为什么是 7 而不是 0?
     *     FIFO 深度 8 字节, 设阈值为 7 意味着:
     *       收到 8 字节 → 触发中断 → ISR 一次读走 7 字节
     *       (留 1 字节给超时中断兜底)
     *
     * TX 中断: 本项目用查询发送, 不使能 TX 中断
     *
     * 超时中断:
     *   TimeoutTime = 255 — 超时时间 (约 2.2ms @115200)
     *   TimeoutIEn  = 1   — 使能
     *   作用: 收到不足阈值的数据后, 超时没新数据则触发 ISR
     *         保证最后一包零碎数据不会无限等待
     */
    u.RXThreshold    = 7;
    u.RXThresholdIEn = 1;

    u.TXThreshold    = 0;
    u.TXThresholdIEn = 0;

    u.TimeoutTime    = 255;
    u.TimeoutIEn     = 1;

    UART_Init(UART0, &u);
}

/* ================================================================
 * do_open — 使能 UART 外设
 * ================================================================ */
static void do_open(void)
{
    UART_Open(UART0);
}

/* ================================================================
 * do_close — 关闭 UART 外设
 * ================================================================ */
static void do_close(void)
{
    UART_Close(UART0);
}

/* ================================================================
 * do_write_byte — 阻塞发送一个字节
 *
 * 查询方式: 写数据寄存器 → 等 TX 完成 → 返回
 * ================================================================ */
static void do_write_byte(uint8_t byte)
{
    UART_WriteByte(UART0, byte);
    while (UART_IsTXBusy(UART0));
}

/* ================================================================
 * do_is_tx_busy — 查询发送是否忙碌
 * ================================================================ */
static int do_is_tx_busy(void)
{
    return UART_IsTXBusy(UART0);
}

/* ================================================================
 * do_set_rx_callback — 注册 RX 数据回调
 *
 * 上层 drv_uart0 在 Init 时注册, ISR 每收一个字节调用一次。
 * ================================================================ */
static void do_set_rx_callback(uart_rx_callback_t cb)
{
    rx_callback = cb;
}

/* ================================================================
 * do_enable_irq — 使能/关闭 NVIC 中断
 *
 * enable = 1: 在 NVIC 中使能 UART0 中断线
 * enable = 0: 关闭
 * ================================================================ */
static void do_enable_irq(int enable)
{
    if (enable)
        NVIC_EnableIRQ(UART0_IRQn);
    else
        NVIC_DisableIRQ(UART0_IRQn);
}

/* ================================================================
 * UART0_Handler — UART0 中断服务程序
 *
 * 两种触发源:
 *   1. RX_THR — FIFO 中数据 ≥ RXThreshold(7) 字节
 *      处理: 从 FIFO 读到剩余 1 字节
 *
 *   2. RX_TOUT — 接收超时 (FIFO 非空但超时未收到新数据)
 *      处理: 读空 FIFO 中所有剩余字节
 *
 * 为什么不一次读空?
 *   中断里读到 FIFO 只剩 1 字节就停, 留给超时中断兜底。
 *   避免在阈值中断里和新到的数据竞争 FIFO 读写指针。
 *
 * 每读到一字节, 调用 rx_callback 通知上层。
 * ================================================================ */
void UART0_Handler(void)
{
    uint32_t chr;

    /* 阈值中断: FIFO 积攒到 7+ 字节 */
    if (UART_INTStat(UART0, UART_IT_RX_THR)) {
        /*
         * FIFO 深度 8, 阈值为 7。
         * 此处条件 > 1 而非 > 0:
         *   读到剩 1 字节留给超时中断处理,
         *   避免恰好和新数据竞争。
         */
        while ((UART0->FIFO & UART_FIFO_RXLVL_Msk) > 1) {
            if (UART_ReadByte(UART0, &chr) == 0 && rx_callback)
                rx_callback((uint8_t)chr);
        }
    }

    /* 超时中断: 收尾最后一包零散数据 */
    if (UART_INTStat(UART0, UART_IT_RX_TOUT)) {
        while (UART_IsRXFIFOEmpty(UART0) == 0) {
            if (UART_ReadByte(UART0, &chr) == 0 && rx_callback)
                rx_callback((uint8_t)chr);
        }
    }
}

/* ================================================================
 * 静态实例 + 导出指针
 *
 *   m_drvp_uart0: 静态结构体, 7 个函数指针指向上面的实现
 *   drvp_uart0  : 全局指针, 上层唯一入口
 * ================================================================ */
static drvp_uart_t m_drvp_uart0 = {
    do_init,
    do_open,
    do_close,
    do_write_byte,
    do_is_tx_busy,
    do_set_rx_callback,
    do_enable_irq,
};

drvp_uart_pt drvp_uart0 = &m_drvp_uart0;
