/**
 * drv_uart0.c — UART0 驱动实现
 *
 * 改造: 使用 drv_loop 接口指针模式替代旧的静态 loopbuf。
 */

#include "drv_uart0.h"

/* UART0 接收环形缓冲 (在 UART0_Init 中分配) */
loopbuf_pt g_uart0_loop = NULL;

void UART0_Init(uint32_t baudrate)
{
    UART_InitStructure u;

    /* 通过驱动层接口创建环形缓冲 (1024 字节) */
    g_uart0_loop = loop->create();
    if (g_uart0_loop == NULL) {
        /* 内存分配失败 — 不应发生 (90KB heap 足够) */
        while (1);
    }

    /* 引脚功能选择 */
    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);

    u.Baudrate       = baudrate;
    u.DataBits       = UART_DATA_8BIT;
    u.Parity         = UART_PARITY_NONE;
    u.StopBits       = UART_STOP_1BIT;

    /* RX 中断: FIFO 中数据 > RXThreshold 时触发 */
    u.RXThreshold    = 7;
    u.RXThresholdIEn = 1;

    /* TX 中断: 不使用 */
    u.TXThreshold    = 0;
    u.TXThresholdIEn = 0;

    /* 超时中断: RX FIFO 非空且超时未收到新数据时触发 */
    u.TimeoutTime    = 255;
    u.TimeoutIEn     = 1;

    UART_Init(UART0, &u);
    UART_Open(UART0);
}

/* UART0 中断服务程序
 *
 * 两种触发:
 *   1. RX_THR — FIFO 收到 ≥ 阈值
 *   2. RX_TOUT — 接收超时 (最后一包数据)
 *
 * 收完数据通过 loop->write() 写入环形缓冲 (ISR 安全,
 * 因为 write 只操作 head 指针, 不阻塞、不调 FreeRTOS API)
 */
void UART0_Handler(void)
{
    uint32_t chr;

    if (UART_INTStat(UART0, UART_IT_RX_THR)) {
        while ((UART0->FIFO & UART_FIFO_RXLVL_Msk) > 1) {
            if (UART_ReadByte(UART0, &chr) == 0)
                loop->write(g_uart0_loop, (uint8_t)chr);
        }
    }

    if (UART_INTStat(UART0, UART_IT_RX_TOUT)) {
        while (UART_IsRXFIFOEmpty(UART0) == 0) {
            if (UART_ReadByte(UART0, &chr) == 0)
                loop->write(g_uart0_loop, (uint8_t)chr);
        }
    }
}

/* printf 重定向到 UART0 (查询发送, 非中断) */
int fputc(int ch, FILE *f)
{
    UART_WriteByte(UART0, ch);
    while (UART_IsTXBusy(UART0));
    return ch;
}
