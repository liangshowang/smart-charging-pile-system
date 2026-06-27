#include "io_config.h"
#include <stdio.h>

/* 全部 IO 引脚定义 */
const io_info_t io_table[IO_COUNT] = {
    { "ELC0  ", IO_ELC0_PORT,   IO_ELC0_PIN,   IO_ELC0_FUNC,   GPIOM, PIN_DIR_OUT },
    { "ELC1  ", IO_ELC1_PORT,   IO_ELC1_PIN,   IO_ELC1_FUNC,   GPIOB, PIN_DIR_OUT },
    { "SLED  ", IO_SLED_PORT,   IO_SLED_PIN,   IO_SLED_FUNC,   GPIOC, PIN_DIR_OUT },
    { "NetRst", IO_NETRST_PORT, IO_NETRST_PIN, IO_NETRST_FUNC, GPION, PIN_DIR_OUT },
    { "fuse0 ", IO_FUSE_PORT,   IO_FUSE_PIN,   IO_FUSE_FUNC,   GPIOC, PIN_DIR_IN  },
};

void io_init_all(void)
{
    uint32_t i;
    printf("[IO INIT] %d pins:\r\n", IO_COUNT);

    for (i = 0; i < IO_COUNT; i++)
    {
        const io_info_t *io = &io_table[i];

        PORT_Init(io->port, io->pin, io->func,
                  (io->dir == PIN_DIR_IN) ? 1 : 0);

        if (io->dir == PIN_DIR_IN)
        {
            GPIO_INIT(io->gpio, io->pin, GPIO_INPUT_PullUp);
            printf("  %s  -> INPUT (pull-up)\r\n", io->name);
        }
        else
        {
            GPIO_INIT(io->gpio, io->pin, GPIO_OUTPUT);
            GPIO_ClrBit(io->gpio, io->pin);
            printf("  %s  -> OUTPUT (init LOW)\r\n", io->name);
        }
    }
}

void io_print_all(void)
{
    uint32_t i;
    for (i = 0; i < IO_COUNT; i++)
    {
        uint32_t val = GPIO_GetBit(io_table[i].gpio, io_table[i].pin);
        printf("  %s : %s\r\n", io_table[i].name, val ? "HIGH" : "LOW ");
    }
}
