/**
 * Step 23: 充电控制 (sock_ctrl)
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
 * 充电控制 (sock_ctrl):
 *   - 状态机: INIT → WAIT_ORDER → CHARGING → FINISHED
 *   - 跨任务通信: message (task_start) → FreeRTOS 队列 → sock_ctrl (task_ctrl)
 *   - 安全检测: 时间到 / 手动停止 / 保险丝熔断 / 无电流
 *   - 结束上报: msg_man->add_event() → 事件池可靠重试 → 服务器 over ACK
 *
 * 软件栈:
 *   App → sock_ctrl → msg_man → net_man → air_man → at_dev → drv_uart1 → 硬件
 *
 * Step 23 新增:
 *   - softpack/sock_ctrl (充电状态机 + 安全检测 + 事件上报)
 *   - 修改 message.c: h_on/h_off 对接 sock_ctrl->send_order()
 *   - 修改 task_ctrl.c: 驱动 sock_ctrl->work()
 *   - 验证: 完整充电周期 (on→充电→off→over ACK)
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
