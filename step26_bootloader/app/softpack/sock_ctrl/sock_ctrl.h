/**
 * sock_ctrl.h — 插座充电控制接口
 *
 * 充电状态机 + 安全检测, 在 task_ctrl 中驱动。
 *
 * 状态机:
 *   INIT → WAIT_ORDER → CHARGING → FINISHED → WAIT_ORDER → ...
 *
 * 安全检测 (CHARGING 期间每次 work 检查):
 *   - 时间到 (充电时长用尽)
 *   - 手动停止 (服务器 off 命令)
 *   - 保险丝熔断 (drv_fuse->is_err)
 *   - 无电流 (HLW8012 脉冲长时间不增长)
 *
 * 跨任务通信:
 *   message 层 (task_start 上下文) 收到 on/off 命令
 *     → sock_ctrl->send_order() 放入 FreeRTOS 队列
 *   task_ctrl 主循环
 *     → sock_ctrl->work() 取出队列 + 驱动状态机
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#ifndef __SOCK_CTRL_H__
#define __SOCK_CTRL_H__

#include <stdint.h>

/* 最大插座数 */
#define SOCK_MAX  2

/* ---- 订单类型 ---- */
typedef enum {
    SOCK_ORDER_START = 0,  /* 开始充电 */
    SOCK_ORDER_STOP  = 1,  /* 停止充电 */
} sock_order_type_t;

/* ---- 订单结构 (message 层发给 sock_ctrl 的队列消息) ---- */
typedef struct {
    sock_order_type_t type;
    int     sock;          /* 插座号 (0-based, 0=左, 1=右) */
    char    ddh[20];       /* 订单号 */
    uint32_t minutes;      /* 充电时长 (分钟, START 有效) */
} sock_order_t;

/* ---- 充电状态 ---- */
typedef enum {
    SOCK_STA_INIT = 0,       /* 初始化 (关继电器, 清状态) */
    SOCK_STA_WAIT_ORDER,     /* 等待订单 (空闲) */
    SOCK_STA_CHARGING,       /* 充电中 (继电器开, 安全监控) */
    SOCK_STA_FINISHED,       /* 充电结束 (继电器关, 上报事件) */
} sock_state_t;

/* ---- 充电结束原因 ---- */
typedef enum {
    SOCK_END_NONE = 0,
    SOCK_END_TIME_UP,        /* 充电时长用尽 */
    SOCK_END_MANUAL,         /* 服务器手动停止 (off 命令) */
    SOCK_END_FUSE,           /* 保险丝熔断 */
    SOCK_END_NO_CURRENT,     /* 无电流 (插头拔出/设备故障) */
} sock_end_reason_t;

/* ---- 插座控制接口 ---- */
typedef struct {
    /* 初始化: 关继电器 + 创建消息队列 */
    void (*init)(void);

    /* 主循环: 处理订单队列 + 推进每个插座的状态机 */
    void (*work)(void);

    /* 发送订单 (message 层调用, 跨任务安全) */
    int  (*send_order)(sock_order_t *order);

    /* 查询是否正在充电 */
    int  (*is_charging)(int sock);

    /* 查询当前状态 (调试用) */
    int  (*get_state)(int sock);

} sock_ctrl_t, *sock_ctrl_pt;

extern sock_ctrl_pt sock_ctrl;

#endif /* __SOCK_CTRL_H__ */
