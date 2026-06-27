/**
 * Step 22: 服务器协议层 (message)
 *
 * 四任务并行架构:
 *   task_start  — 初始化硬件 + 驱动入网 + 协议引擎
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
 *       → while(1) { net_man->work(); }  ← 状态机 + 协议引擎
 *
 * 网络状态机 (net_man):
 *   Phase 0 (RESET):  硬件复位 → 等 45s → AT 握手
 *   Phase 1 (INIT):   12 条 AT 命令
 *   Phase 2 (CONNECT): CIPSTART → LOGIN → 等 Logon
 *   Phase 3 (READY):   msg_man->work()  ← 协议引擎接管
 *
 * 协议引擎 (message):
 *   - 命令行派发: 17 条服务器命令 (Logon/pong/on/off/time/reboot/...)
 *   - 心跳保活: PING 每 10s, 超时 30s 重连
 *   - 事件池:   固定数组, 可靠重试上报 (over ACK)
 *
 * 软件栈 (4G 通信):
 *   App → msg_man → net_man → air_man → at_dev → drv_uart1 → 硬件
 *
 * Step 22 新增:
 *   - softpack/message (协议引擎: 解析/派发/心跳/事件池)
 *   - 17 条服务器命令处理
 *   - 验证: Python TCP 服务端收发完整协议
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
