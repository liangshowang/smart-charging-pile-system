/**
 * Step 20: 4G 模块管理层 (air_man)
 *
 * 四任务并行架构:
 *   task_start  — 初始化硬件 + 4G 设备 + 创建其他任务 + 4G 测试循环
 *   task_host   — LED刷新 + 命令行 + 灯板动画 + 日志输出
 *   task_ctrl   — 充电控制 + OTA (占位)
 *   task_listen — 保险丝检测 (5ms) + HLW8012 脉冲轮询 (计量)
 *
 * 任务间关系:
 *   main() → task_start->create()
 *     → uart1->init → air_man->create_dev → show_banner
 *       → task_host->create()     (P3, 4096)
 *       → task_listen->create()   (P5, 1024)
 *       → task_ctrl->create()     (P3, 2048)
 *       → 4G test loop (AT → CPIN → CSQ, 带响应解析)
 *
 * 软件栈 (4G 通信):
 *   App → air_man → at_dev → drv_uart1 → drvp_uart1 → 硬件
 *
 * Step 20 新增:
 *   - softpack/air_man (4G 设备管理层: 便捷命令封装 + 响应解析)
 *   - air724->AT()/CPIN()/CSQ()/CREG()/CGATT()
 *   - air724 类型升级为 air_dev_pt (含 at_dev + 命令方法)
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
