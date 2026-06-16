/**
 * Step 1: 空白 MDK 工程 + 点灯
 *
 * 目标: 建立最小可编译 MDK 工程，驱动 SLED (GPIOC PIN4) 以 ~1Hz 闪烁
 * 芯片: SWM320RET7 (ARM Cortex-M4, 120MHz / 实际 110.592MHz)
 * 引脚: C4 = 状态指示灯 (封装引脚17)
 */

#include "SWM320.h"

/*----------------------------------------------------------------------------
 * 简易阻塞延时 (~0.5s)
 *----------------------------------------------------------------------------*/
void delay_approx(void)
{
    volatile uint32_t d = 2000000;
    while (d--);
}

/*----------------------------------------------------------------------------
 * UART0 初始化 — printf 调试输出
 * PA2=RX, PA3=TX, 115200-8-N-1
 *----------------------------------------------------------------------------*/
void SerialInit(void)
{
    UART_InitStructure u;

    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);

    u.Baudrate        = 115200;
    u.DataBits        = UART_DATA_8BIT;
    u.Parity          = UART_PARITY_NONE;
    u.StopBits        = UART_STOP_1BIT;
    u.RXThreshold     = 3;
    u.RXThresholdIEn  = 0;
    u.TXThreshold     = 3;
    u.TXThresholdIEn  = 0;
    u.TimeoutTime     = 10;
    u.TimeoutIEn      = 0;
    UART_Init(UART0, &u);
    UART_Open(UART0);
}

/*----------------------------------------------------------------------------
 * printf 重定向到 UART0 (需勾选 MDK "Use MicroLIB")
 *----------------------------------------------------------------------------*/
int fputc(int ch, FILE *f)
{
    UART_WriteByte(UART0, ch);
    while (UART_IsTXBusy(UART0));
    return ch;
}

/*----------------------------------------------------------------------------
 * 主函数
 *----------------------------------------------------------------------------*/
int main(void)
{
    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 1: Hello SWM320 -- Blinky!\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("========================================\r\n\r\n");

    GPIO_Init(GPIOC, PIN4, 1, 0, 0);

    while (1)
    {
        GPIO_SetBit(GPIOC, PIN4);
        printf("[ON]  LED at C4 is HIGH\r\n");
        delay_approx();

        GPIO_ClrBit(GPIOC, PIN4);
        printf("[OFF] LED at C4 is LOW\r\n");
        delay_approx();
    }
}
