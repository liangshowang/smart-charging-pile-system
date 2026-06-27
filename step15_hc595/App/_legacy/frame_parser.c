#include "SWM320.h"
#include "loopbuf.h"
#include "drv_uart0.h"
#include "frame_parser.h"
#include "io_config.h"
#include <stdio.h>

enum {
    STATE_WAIT_HEAD = 0,
    STATE_WAIT_BODY,
};

static int      rx_state = STATE_WAIT_HEAD;
static int      rx_idx   = 0;
static uint8_t  rx_buf[FRAME_LEN];

/* checksum: sum of bytes 1~9, lower 8 bits */
static uint8_t calc_checksum(const uint8_t *buf)
{
    uint32_t sum = 0;
    int i;
    for (i = 1; i <= 9; i++)
        sum += buf[i];
    return (uint8_t)(sum & 0xFF);
}

/* command dispatch */
static void dispatch(const uint8_t *buf)
{
    uint8_t cmd = buf[1];

    printf("[CMD] valid frame, CMD=0x%02X\r\n", cmd);

    switch (cmd)
    {
    case CMD_LED_ON:
        printf("  -> SLED ON\r\n");
        GPIO_SetBit(GPIOC, PIN4);
        break;

    case CMD_LED_OFF:
        printf("  -> SLED OFF\r\n");
        GPIO_ClrBit(GPIOC, PIN4);
        break;

    case CMD_RELAY0:
        if (buf[2])
        {
            printf("  -> ELC0 ON\r\n");
            GPIO_SetBit(GPIOM, PIN2);
        }
        else
        {
            printf("  -> ELC0 OFF\r\n");
            GPIO_ClrBit(GPIOM, PIN2);
        }
        break;

    case CMD_RELAY1:
        if (buf[2])
        {
            printf("  -> ELC1 ON\r\n");
            GPIO_SetBit(GPIOB, PIN12);
        }
        else
        {
            printf("  -> ELC1 OFF\r\n");
            GPIO_ClrBit(GPIOB, PIN12);
        }
        break;

    case CMD_PING:
        printf("  -> PONG\r\n");
        break;

    default:
        printf("  -> unknown CMD 0x%02X\r\n", cmd);
        break;
    }
}

/* frame parser state machine (called from main loop) */
void frame_parser_poll(void)
{
    uint8_t byte;
    uint8_t expected;

    while (loopbuf_read(&g_uart0_rxbuf, &byte) == 1)
    {
        switch (rx_state)
        {
        case STATE_WAIT_HEAD:
            if (byte == FRAME_HEAD)
            {
                rx_buf[0] = byte;
                rx_idx    = 1;
                rx_state  = STATE_WAIT_BODY;
            }
            /* non-header bytes discarded */
            break;

        case STATE_WAIT_BODY:
            rx_buf[rx_idx++] = byte;

            if (rx_idx >= FRAME_LEN)
            {
                /* verify tail */
                if (rx_buf[FRAME_LEN - 1] != FRAME_TAIL)
                {
                    printf("[ERR] bad tail: exp 0x5A, got 0x%02X\r\n",
                           rx_buf[FRAME_LEN - 1]);
                    rx_state = STATE_WAIT_HEAD;
                    rx_idx   = 0;
                    break;
                }

                /* verify checksum */
                expected = calc_checksum(rx_buf);
                if (rx_buf[FRAME_LEN - 2] != expected)
                {
                    printf("[ERR] bad checksum: exp 0x%02X, got 0x%02X\r\n",
                           expected, rx_buf[FRAME_LEN - 2]);
                    rx_state = STATE_WAIT_HEAD;
                    rx_idx   = 0;
                    break;
                }

                /* valid frame -> dispatch */
                dispatch(rx_buf);

                /* back to waiting for header */
                rx_state = STATE_WAIT_HEAD;
                rx_idx   = 0;
            }
            break;

        default:
            rx_state = STATE_WAIT_HEAD;
            rx_idx   = 0;
            break;
        }
    }
}
