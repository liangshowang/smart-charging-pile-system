/**
 * Step 4: SysTick 定时器 + 时间驱动状态机
 *
 * 目标: 用 SysTick 1ms 中断替代阻塞延时，实现精确时间驱动的
 *       继电器状态机——裸机 → RTOS 的关键一步。
 *
 * 模块:
 *   systick.c       — SysTick 中断 + 全局 tick 计数器
 *   state_machine.c — 时间驱动的继电器状态机
 *   io_config.c     — IO 引脚初始化
 *   serial.c        — UART0 调试输出
 */

#include "SWM320.h"
#include "serial.h"
#include "io_config.h"
#include "systick.h"
#include "state_machine.h"

int main(void)
{
    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 4: SysTick 定时器 + 状态机\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("  SysTick  : 1ms per tick\r\n");
    printf("========================================\r\n\r\n");

    /* 初始化 IO */
    io_init_all();

    /* 启动 SysTick: SystemCoreClock/1000 = 每 1ms 中断一次 */
    SysTick_Config(SystemCoreClock / 1000);

    /* 初始化状态机 */
    sm_init();

    printf("[MAIN] 进入主循环，SysTick 驱动状态机...\r\n\r\n");

    while (1)
    {
        sm_work();
    }
}
