#include "drv_uart0.h"

loopbuf_t g_uart0_rxbuf;

void UART0_Init(uint32_t baudrate)
{
    UART_InitStructure u;

    loopbuf_init(&g_uart0_rxbuf);

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

    /* 超时中断: RX FIFO 非空且超时未收到新数据时触发 (保证不丢最后几个字节) */
    u.TimeoutTime    = 255;
    u.TimeoutIEn     = 1;

    UART_Init(UART0, &u);
    UART_Open(UART0);
}

/* UART0 中断服务程序
 * 两种触发条件:
 *   1. RX_THR — FIFO 收到超过阈值的字节
 *   2. RX_TOUT — 接收超时 (最后一包数据)
 * 将收到的每个字节写入环形缓冲
 */
void UART0_Handler(void)
{
    uint32_t chr;

    /* RX FIFO 阈值中断 */
    if (UART_INTStat(UART0, UART_IT_RX_THR))
    {
        while ((UART0->FIFO & UART_FIFO_RXLVL_Msk) > 1)
        {
            if (UART_ReadByte(UART0, &chr) == 0)
                loopbuf_write(&g_uart0_rxbuf, (uint8_t)chr);
        }
    }

    /* RX 超时中断 (最后一两个字节也用此方式收尾) */
    if (UART_INTStat(UART0, UART_IT_RX_TOUT))
    {
        while (UART_IsRXFIFOEmpty(UART0) == 0)
        {
            if (UART_ReadByte(UART0, &chr) == 0)
                loopbuf_write(&g_uart0_rxbuf, (uint8_t)chr);
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
