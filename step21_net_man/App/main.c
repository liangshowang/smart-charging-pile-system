/**
 * Step 21: 4G 网络初始化状态机 (net_man)
 *
 * 四任务并行架构:
 *   task_start  — 初始化硬件 + 驱动 net_man 状态机入网
 *   task_host   — LED刷新 + 命令行 + 灯板动画 + 日志输出
 *   task_ctrl   — 充电控制 + OTA (占位)
 *   task_listen — 保险丝检测 (5ms) + HLW8012 脉冲轮询 (计量)
 *
 * 任务间关系:
 *   main() → task_start->create()
 *     → uart1->init → air_man->create_dev → net_man->init
 *       → show_banner
 *       → task_host->create()     (P3, 4096)
 *       → task_listen->create()   (P5, 1024)
 *       → task_ctrl->create()     (P3, 2048)
 *       → while(1) { net_man->work(); }  ← 驱动三阶段状态机
 *
 * 网络状态机:
 *   Phase 0 (RESET):  硬件复位 → 等 45s → AT 握手
 *   Phase 1 (INIT):   12 条 AT 命令 (CGMI→...→CIFSR, 每步3次重试)
 *   Phase 2 (CONNECT): CIPSTART→LOGIN→等 Logon
 *   Phase 3 (READY):   网络就绪, 等待后续协议层
 *
 * 软件栈 (4G 通信):
 *   App → net_man → air_man → at_dev → drv_uart1 → drvp_uart1 → 硬件
 *
 * Step 21 新增:
 *   - softpack/net_man (三阶段网络状态机)
 *   - air_man 扩展: CGMI/CGMR/CIPMODE/CIPMUX/CSTT/CIICR/CIFSR/CIPSTART/CIPCLOSE
 *   - 入网验证: 插SIM卡→约2分钟→TCP连接成功→收到Logon
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
