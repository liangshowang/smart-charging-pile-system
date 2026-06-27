/**
 * Step 3: SDK 库级 GPIO — 全部 IO 引脚
 *
 * 目标: 用官方 SDK API (GPIO_Init/SetBit/ClrBit/GetBit/PORT_Init)
 *       统一管理全部 5 个 IO，为后续定时器/中断/FreeRTOS 打好基础。
 *
 * 模块划分:
 *   serial.c   — UART0 初始化 + printf 重定向
 *   io_config.c — IO 引脚表 + 初始化 + 状态打印
 *   delay.c    — 阻塞延时函数
 *   main.c     — 测试流程 (本文件)
 */

#include "SWM320.h"
#include "serial.h"
#include "io_config.h"
#include "delay.h"

int main(void)
{
    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 3: SDK GPIO — 全部 IO 引脚\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("========================================\r\n\r\n");

    io_init_all();
    printf("\r\n[STATUS] 初始状态:\r\n");
    io_print_all();
    printf("\r\n");

    while (1)
    {
        /* 插座1 继电器 */
        printf(">>> ELC0 HIGH (插座1 吸合)\r\n");
        GPIO_SetBit(GPIOM, PIN2);
        io_print_all();
        delay_long();

        printf(">>> ELC0 LOW\r\n");
        GPIO_ClrBit(GPIOM, PIN2);
        io_print_all();
        delay_long();

        /* 插座2 继电器 */
        printf(">>> ELC1 HIGH (插座2 吸合)\r\n");
        GPIO_SetBit(GPIOB, PIN12);
        io_print_all();
        delay_long();

        printf(">>> ELC1 LOW\r\n");
        GPIO_ClrBit(GPIOB, PIN12);
        io_print_all();
        delay_long();

        /* 状态灯 */
        printf(">>> SLED ON\r\n");
        GPIO_SetBit(GPIOC, PIN4);
        io_print_all();
        delay_approx();

        printf(">>> SLED OFF\r\n");
        GPIO_ClrBit(GPIOC, PIN4);
        io_print_all();
        delay_approx();

        /* 4G 模块复位 */
        printf(">>> NetRst HIGH\r\n");
        GPIO_SetBit(GPION, PIN7);
        io_print_all();
        delay_approx();

        printf(">>> NetRst LOW\r\n");
        GPIO_ClrBit(GPION, PIN7);
        io_print_all();
        delay_approx();

        printf("===== 一轮完成 =====\r\n\r\n");
    }
}
