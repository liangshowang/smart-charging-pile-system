/**
 * drvp_uart1.c — UART1 端口驱动实现 (SWM320)
 *
 * 用于与 4G 模块 (AIR724) 通信的 UART1。
 * 架构与 drvp_uart0.c 完全一致:
 *   drvp_uart1 (全局指针) → m_drvp_uart1 (静态实例)
 *     → { Init, Open, Close, WriteByte, IsTXBusy,
 *         SetRxCallback, SetTxCallback, StartTx, StopTx, EnableIRQ }
 *       → SDK 函数
 *
 * ISR (UART1_Handler) 归端口层:
 *   三种触发源:
 *     RX_THR  — RX FIFO ≥ 7 字节
 *     RX_TOUT — RX 超时 (最后一包收尾)
 *     TX_THR  — TX FIFO 已空 (可以继续发送)
 *
 * 引脚 (V5_4): PA4=RX, PA5=TX
 */

#include "SWM320.h"
#include "drvp_uart1.h"
#include <stddef.h>

/* ================================================================
 * 模块级变量
 * ================================================================ */
static uart1_rx_callback_t rx1_callback = NULL;
static uart1_tx_callback_t tx1_callback = NULL;

/* ================================================================
 * do_init — 硬件初始化
 *
 *   RX: 阈值中断(7字节) + 超时中断
 *   TX: 配置 TX 空中断 (TXThreshold=0), 但默认关闭
 *       只有上层调 StartTx 后才使能
 * ================================================================ */
static void do_init(uint32_t baudrate)
{
    UART_InitStructure u;

    /* 引脚功能选择 (V5_4) */
    PORT_Init(PORTA, PIN4, FUNMUX0_UART1_RXD, 1);   /* PA4: RX, 上拉输入 */
    PORT_Init(PORTA, PIN5, FUNMUX1_UART1_TXD, 0);   /* PA5: TX, 推挽输出 */

    u.Baudrate       = baudrate;
    u.DataBits       = UART_DATA_8BIT;
    u.Parity         = UART_PARITY_NONE;
    u.StopBits       = UART_STOP_1BIT;

    /* RX: 阈值中断 + 超时中断 */
    u.RXThreshold    = 7;
    u.RXThresholdIEn = 1;

    /*
     * TX: 配置 FIFO 空中断 (Threshold=0), 但默认关闭
     *
     * TXThreshold=0 含义: TX FIFO 从有数据变为空时触发中断
     * 初始状态下 FIFO 本来就是空的, 如果直接使能会不停触发中断。
     * 所以 TXThresholdIEn=0, 等上层数据就绪后再手动打开。
     */
    u.TXThreshold    = 0;
    u.TXThresholdIEn = 0;

    u.TimeoutTime    = 255;
    u.TimeoutIEn     = 1;

    UART_Init(UART1, &u);
}

/* ================================================================
 * do_open — 使能 UART 外设
 * ================================================================ */
static void do_open(void)
{
    UART_Open(UART1);
}

/* ================================================================
 * do_close — 关闭 UART 外设
 * ================================================================ */
static void do_close(void)
{
    UART_Close(UART1);
}

/* ================================================================
 * do_write_byte — 阻塞发送一个字节 (查询方式)
 * ================================================================ */
static void do_write_byte(uint8_t byte)
{
    UART_WriteByte(UART1, byte);
    while (UART_IsTXBusy(UART1));
}

/* ================================================================
 * do_is_tx_busy — 查询发送是否忙碌
 * ================================================================ */
static int do_is_tx_busy(void)
{
    return UART_IsTXBusy(UART1);
}

/* ================================================================
 * do_set_rx_callback — 注册 RX 数据回调
 * ================================================================ */
static void do_set_rx_callback(uart1_rx_callback_t cb)
{
    rx1_callback = cb;
}

/* ================================================================
 * do_set_tx_callback — 注册 TX 数据回调
 * ================================================================ */
static void do_set_tx_callback(uart1_tx_callback_t cb)
{
    tx1_callback = cb;
}

/* ================================================================
 * do_start_tx — 启动中断发送
 *
 * 写入第一个字节到 TX FIFO, 然后使能 TX 空中断。
 * 后续字节由 ISR 通过 tx1_callback 索要。
 * ================================================================ */
static void do_start_tx(uint8_t first_byte)
{
    UART_WriteByte(UART1, first_byte);
    UART1->CTRL |= UART_CTRL_TXIE_Msk;
}

/* ================================================================
 * do_stop_tx — 关闭 TX 空中断
 * ================================================================ */
static void do_stop_tx(void)
{
    UART1->CTRL &= ~UART_CTRL_TXIE_Msk;
}

/* ================================================================
 * do_enable_irq — 使能/关闭 NVIC 中断
 * ================================================================ */
static void do_enable_irq(int enable)
{
    if (enable)
        NVIC_EnableIRQ(UART1_IRQn);
    else
        NVIC_DisableIRQ(UART1_IRQn);
}

/* ================================================================
 * UART1_Handler — UART1 中断服务程序
 *
 * 三种触发源:
 *   1. RX_THR  — RX FIFO ≥ 7 字节
 *   2. RX_TOUT — RX 超时
 *   3. TX_THR  — TX FIFO 已空 (有数据要发时)
 *
 * TX 处理逻辑:
 *   tx1_callback(&byte) 从环形缓冲取下一个字节
 *     → 返回 1: 写入 TX FIFO, 继续循环
 *     → 返回 0: 环形缓冲空了, 关闭 TX 空中断, 退出
 * ================================================================ */
void UART1_Handler(void)
{
    uint32_t chr;

    /* ---- RX 阈值中断 ---- */
    if (UART_INTStat(UART1, UART_IT_RX_THR)) {
        while ((UART1->FIFO & UART_FIFO_RXLVL_Msk) > 1) {
            if (UART_ReadByte(UART1, &chr) == 0 && rx1_callback)
                rx1_callback((uint8_t)chr);
        }
    }

    /* ---- RX 超时中断 ---- */
    if (UART_INTStat(UART1, UART_IT_RX_TOUT)) {
        while (UART_IsRXFIFOEmpty(UART1) == 0) {
            if (UART_ReadByte(UART1, &chr) == 0 && rx1_callback)
                rx1_callback((uint8_t)chr);
        }
    }

    /* ---- TX 空中断 ---- */
    if (UART_INTStat(UART1, UART_IT_TX_THR)) {
        if (tx1_callback) {
            uint8_t byte;
            while (!UART_IsTXFIFOFull(UART1)) {
                if (tx1_callback(&byte)) {
                    UART_WriteByte(UART1, byte);
                } else {
                    do_stop_tx();
                    break;
                }
            }
        }
    }
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drvp_uart1_t m_drvp_uart1 = {
    do_init,
    do_open,
    do_close,
    do_write_byte,
    do_is_tx_busy,
    do_set_rx_callback,
    do_set_tx_callback,
    do_start_tx,
    do_stop_tx,
    do_enable_irq,
};

drvp_uart1_pt drvp_uart1 = &m_drvp_uart1;
