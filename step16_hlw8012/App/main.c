/**
 * Step 16: HLW8012 计量芯片驱动
 *
 * 四任务并行架构:
 *   task_start  — 初始化硬件 + 创建其他任务 + 主调度循环
 *   task_host   — LED刷新 + 命令行 + 灯板动画 + 日志输出
 *   task_ctrl   — 充电控制 + OTA (占位)
 *   task_listen — HLW8012 脉冲计数轮询 (计量)
 *
 * 任务间关系:
 *   main() → task_start->create()
 *     → uart0->init → io_init_all → show_banner
 *       → task_host->create()     (P3, 4096)
 *       → task_listen->create()   (P5, 1024)
 *       → task_ctrl->create()     (P3, 2048)
 *
 * 相比 Step 12 的变化:
 *   - vBlinkTask + vCmdTask 合并到 task_host
 *   - xTickQueue 移除 (不再需要队列演示)
 *   - main() 简化为 3 行: 初始化 → 创建 start 任务 → 启动调度器
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
