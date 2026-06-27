#include "SWM320.h"
#include "serial.h"

void SerialInit(void)
{
    UART_InitStructure u;

    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);

    u.Baudrate       = 115200;
    u.DataBits       = UART_DATA_8BIT;
    u.Parity         = UART_PARITY_NONE;
    u.StopBits       = UART_STOP_1BIT;
    u.RXThreshold    = 3;
    u.RXThresholdIEn = 0;
    u.TXThreshold    = 3;
    u.TXThresholdIEn = 0;
    u.TimeoutTime    = 10;
    u.TimeoutIEn     = 0;
    UART_Init(UART0, &u);
    UART_Open(UART0);
}

int fputc(int ch, FILE *f)
{
    UART_WriteByte(UART0, ch);
    while (UART_IsTXBusy(UART0));
    return ch;
}
