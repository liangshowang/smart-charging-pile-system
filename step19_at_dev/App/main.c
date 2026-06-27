/**
 * Step 19: 4G 模块 AT 命令引擎 (at_dev)
 *
 * 四任务并行架构:
 *   task_start  — 初始化硬件 + AT 引擎 + 创建其他任务 + AT 测试循环
 *   task_host   — LED刷新 + 命令行 + 灯板动画 + 日志输出
 *   task_ctrl   — 充电控制 + OTA (占位)
 *   task_listen — 保险丝检测 (5ms) + HLW8012 脉冲轮询 (计量)
 *
 * 任务间关系:
 *   main() → task_start->create()
 *     → uart0->init → sysprt->init → io_init_all
 *       → uart1->init → at_man->create → show_banner
 *       → task_host->create()     (P3, 4096)
 *       → task_listen->create()   (P5, 1024)
 *       → task_ctrl->create()     (P3, 2048)
 *       → AT test loop (AT → AT+CPIN? → AT+CSQ)
 *
 * Step 19 新增:
 *   - drvp_uart1 / drv_uart1 (UART1 双环形缓冲, 接 4G 模块)
 *   - softpack/at_dev (AT 命令引擎: send_cmd / auto_baud / timeout)
 *   - UART1 引脚: PA4=RX, PA5=TX (V5_4 板)
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_inc.h"

/* ================================================================
 * main — 最小入口
 *
 * 只做三件事:
 *   1. 系统初始化 (时钟)
 *   2. 创建启动任务 (task_start 会完成所有后续初始化)
 *   3. 启动 FreeRTOS 调度器
 *
 * 不再直接操作 GPIO/UART — 全部交给任务框架。
 * ================================================================ */
int main(void)
{
    SystemInit();

    /* 创建启动任务 — 此后一切由 task_start 接管 */
    task_start->create(NULL, NULL);

    /* 启动调度器 — 永不返回 */
    vTaskStartScheduler();

    /* 不应到达 */
    while (1);
}
