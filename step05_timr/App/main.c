/**
 * Step 5: 硬件定时器 TIMR0 — 双定时器精度对比
 *
 * 目标: 配置 TIMR0 与 SysTick 同时 1ms 中断，对比两个计数器，
 *       验证硬件定时器精度。为后续 PWM/输入捕获打基础。
 *
 * 模块:
 *   systick.c       — SysTick 1ms 中断 (内核定时器)
 *   timr.c          — TIMR0 1ms 中断 (外设定时器)
 *   state_machine.c — 继电器状态机
 *   io_config.c     — IO 初始化
 *   serial.c        — UART0 调试输出
 */

#include "SWM320.h"
#include "serial.h"
#include "io_config.h"
#include "systick.h"
#include "timr.h"
#include "state_machine.h"

int main(void)
{
    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 5: TIMR0 + SysTick 双定时器\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("========================================\r\n\r\n");

    io_init_all();

    /* 启动 SysTick (内核定时器) */
    SysTick_Config(SystemCoreClock / 1000);

    /* 启动 TIMR0 (外设定时器, 32位)
     * TIMR_Init(TIMRx, mode, period, int_en)
     *   mode=TIMR_MODE_TIMER → 定时器模式 (非计数器)
     *   period=SystemCoreClock/1000 → 每1ms溢出一次
     *   int_en=1 → 使能中断
     */
    TIMR_Init(TIMR0, TIMR_MODE_TIMER, SystemCoreClock / 1000, 1);
    TIMR_Start(TIMR0);

    sm_init();

    printf("[MAIN] 双定时器已启动，每 5s 对比一次:\r\n");
    printf("  SysTick (24bit)  vs  TIMR0 (32bit)\r\n\r\n");

    while (1)
    {
        sm_work();

        /* 每 5 秒打印一次双计数器对比 */
        static uint32_t last_report = 0;
        uint32_t now = get_systick();
        if ((now - last_report) >= 5000)
        {
            last_report = now;
            uint32_t st = get_systick();
            uint32_t tt = get_timr_tick();
            int32_t  diff = (int32_t)(st - tt);
            printf("[%5d ms] SysTick=%d  TIMR0=%d  diff=%d\r\n",
                   now, st, tt, diff);
        }
    }
}
