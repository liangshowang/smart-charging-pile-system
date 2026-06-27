/**
 * Step 6: 中断驱动 UART + 环形缓冲 + 帧解析
 *
 * 目标: 打通 UART RX 中断 → 环形缓冲 → 帧解析 → 命令派发的完整数据通路。
 *       后续命令行、AT 引擎、服务器协议都在此架构上扩展。
 *
 * 模块:
 *   loopbuf.c      — 环形缓冲 (256 字节, 静态分配)
 *   drv_uart0.c    — UART0 中断接收 + 查询发送
 *   frame_parser.c — 12 字节帧解析 + 命令派发
 *   io_config.c    — IO 初始化
 *   systick.c      — 系统滴答
 */

#include "SWM320.h"
#include "drv_uart0.h"
#include "io_config.h"
#include "systick.h"
#include "frame_parser.h"

int main(void)
{
    SystemInit();

    /* 先初始化 IO 和 SysTick */
    io_init_all();
    SysTick_Config(SystemCoreClock / 1000);

    /* UART0 初始化 (使能 RX 中断) */
    UART0_Init(115200);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 6: UART IRQ + RingBuffer + Frame\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("  Frame   : 12B [0xA5][CMD][8B][SUM][0x5A]\r\n");
    printf("  Commands: 0x01=LED_ON  0x02=LED_OFF\r\n");
    printf("            0x03=ELC0    0x04=ELC1\r\n");
    printf("            0xAA=PING\r\n");
    printf("========================================\r\n\r\n");

    while (1)
    {
        /* 从环形缓冲取字节 → 拼帧 → 派发命令 */
        frame_parser_poll();
    }
}
