/**
 * Step 7: 交互式命令行
 *
 * 目标: 在 UART 中断 + 环形缓冲基础上实现交互式命令行终端,
 *       支持退格/回显/参数分割/命令表匹配。
 *
 * 模块:
 *   cmd_line.c    — 命令行状态机 + 命令表 (新增)
 *   loopbuf.c     — 环形缓冲
 *   drv_uart0.c   — UART0 中断接收 + 查询发送
 *   io_config.c   — IO 初始化
 *   systick.c     — 系统滴答
 */

#include "SWM320.h"
#include "drv_uart0.h"
#include "io_config.h"
#include "systick.h"
#include "cmd_line.h"

int main(void)
{
    SystemInit();

    io_init_all();
    SysTick_Config(SystemCoreClock / 1000);
    UART0_Init(115200);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 7: Interactive Command Line\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("  Type 'help' for available commands\r\n");
    printf("========================================\r\n");

    while (1) {
        cmd_line_work();
    }
}
