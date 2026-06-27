/**
 * net_man.h — 4G 网络初始化状态机接口
 *
 * 在 air_man 之上管理 4G 模块的完整入网流程。
 *
 * 三阶段状态机:
 *   Phase 0 (RESET)   — 硬件复位 + 等待模块启动 (约 45s)
 *   Phase 1 (INIT)    — AT 命令序列初始化网络 (12步, 每步3次重试)
 *   Phase 2 (CONNECT) — TCP 连接服务器 + LOGIN + 等待 Logon
 *   Phase 3 (READY)   — 网络就绪, 可进行数据通信
 *
 * 使用方式:
 *   net_man->init();          // 初始化状态机
 *   while (1) {
 *       net_man->work();      // 驱动状态机 (每步一个 AT 命令, 阻塞)
 *       vTaskDelay(...);      // 控制轮询频率
 *       if (net_man->is_ready()) break;  // 网络就绪
 *   }
 *
 * 分层关系:
 *   App 层           → net_man->work()        (状态机调度)
 *   softpack/net_man → air724->xxx()          (AT 命令封装)
 *   softpack/air_man → at_man->send_cmd()     (通用 AT 引擎)
 *   softpack/at_dev  → uart1->send/read       (UART 收发)
 */

#ifndef __NET_MAN_H__
#define __NET_MAN_H__

#include <stdint.h>

/* ---- 状态机阶段定义 ---- */
#define NET_PHASE_RESET    0   /* 复位阶段: 硬件复位 + 等待启动 */
#define NET_PHASE_INIT     1   /* 初始化阶段: AT命令序列 */
#define NET_PHASE_CONNECT  2   /* 连接阶段: TCP + LOGIN */
#define NET_PHASE_READY    3   /* 就绪阶段: 网络可用 */

/* ---- 每步最大重试次数 ---- */
#define NET_CMD_MAX_RETRY  3

/* ---- 网络管理器接口 ---- */
typedef struct {
    /* 初始化状态机 (设备必须已由 air_man->create_dev 创建) */
    void (*init)(void);

    /* 驱动状态机前进一步 (阻塞执行一条 AT 命令)
     * 应在任务循环中反复调用, 直到 is_ready() 返回 1 */
    void (*work)(void);

    /* 查询网络是否就绪 (phase == NET_PHASE_READY) */
    int  (*is_ready)(void);

    /* 获取当前阶段和步骤 (调试用) */
    void (*get_state)(int *phase, int *step, int *reconnect);

    /* 强制复位 (任意阶段回到 RESET) */
    void (*force_reset)(void);

} net_man_t, *net_man_pt;

/* ---- 全局接口指针 ---- */
extern net_man_pt net_man;

#endif /* __NET_MAN_H__ */
