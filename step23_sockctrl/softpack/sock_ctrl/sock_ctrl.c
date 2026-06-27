/**
 * sock_ctrl.c — 充电状态机实现
 *
 * 每个插座一个 sock_ctx_t, 包含:
 *   - 当前状态 / 结束原因
 *   - 订单号 / 计划时长 / 开始时刻
 *   - HLW8012 脉冲快照 (用于无电流检测)
 *
 * 跨任务通信:
 *   一个 FreeRTOS 队列 (sock_order_t, 4 槽):
 *     message 层 (task_start) → send_order() → 入队
 *     task_ctrl → work() → process_orders() → 出队
 *
 * 状态转换:
 *
 *   INIT ─[自动]→ WAIT_ORDER
 *                   │
 *                   ├─ 收到 START 订单 → relay_on → CHARGING
 *                   │                              │
 *                   │  ┌─ 时间到 ─────────┐        │
 *                   │  ├─ 手动停止 ───────┤        │
 *                   │  ├─ 保险丝熔断 ─────┤→ FINISHED
 *                   │  ├─ 无电流 ────────┘        │
 *                   │  │                     relay_off
 *                   │  │                     msg_man->add_event(over)
 *                   │  │                          │
 *                   │  └──────── 回到 WAIT_ORDER ←┘
 *                   │
 *                   └─ 收到 STOP 在非 CHARGING 状态 → 忽略
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#include "sock_ctrl.h"
#include "drv_gpio.h"
#include "drv_hlw8012.h"
#include "drv_fuse.h"
#include "led_board.h"
#include "message.h"
#include "io_config.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

/* ---- 常量 ---- */
#define ORDER_QUEUE_LEN    4      /* 订单队列容量 */
#define CHECK_INTERVAL     500    /* 安全检查周期 (ms) */
#define NO_CURRENT_DELAY   10000  /* 充电开始后多久开始检测无电流 (ms) */
#define NO_CURRENT_WINDOW  15000  /* 无脉冲超过此时间判定无电流 (ms) */
#define PULSE_CHECK_MIN    5000   /* 两次脉冲检查的最小间隔 (ms) */

/* ---- 单个插座的运行时上下文 ---- */
typedef struct {
    int       state;            /* 当前状态 (sock_state_t) */
    int       end_reason;       /* 结束原因 (sock_end_reason_t) */
    uint32_t  start_tick;       /* 充电开始时刻 (FreeRTOS tick) */
    uint32_t  duration_min;     /* 计划充电时长 (分钟) */
    uint32_t  last_pulse;       /* 上次脉冲计数值 (快照) */
    uint32_t  last_pulse_tick;  /* 上次脉冲增长的时刻 */
    uint32_t  last_check_tick;  /* 上次安全检查时刻 */
    char      ddh[20];          /* 订单号 */
} sock_ctx_t;

/* ---- 内部状态 ---- */
static sock_ctx_t    g_sock[SOCK_MAX];
static QueueHandle_t g_order_queue = NULL;

/* ================================================================
 * 继电器控制
 *
 * 硬件映射:
 *   插座 0 (左, 用户看是 1) → IO_ELC1 (B12, PIN_ELC1)
 *   插座 1 (右, 用户看是 2) → IO_ELC0 (M2,  PIN_ELC0)
 *   (与参考代码 V5_5 一致)
 * ================================================================ */
static void relay_on(int sock)
{
    switch (sock) {
    case 0:
        drv_gpio->write(PIN_ELC1, PIN_HIGH);
        break;
    case 1:
        drv_gpio->write(PIN_ELC0, PIN_HIGH);
        break;
    default:
        break;
    }
}

static void relay_off(int sock)
{
    switch (sock) {
    case 0:
        drv_gpio->write(PIN_ELC1, PIN_LOW);
        break;
    case 1:
        drv_gpio->write(PIN_ELC0, PIN_LOW);
        break;
    default:
        break;
    }
}

/* ================================================================
 * 充电结束 — 统一出口
 *
 * 无论什么原因 (时间到/手动/保险丝/无电流),
 * 都走这里: 关继电器 → 改LED → 上报事件 → 回空闲
 * ================================================================ */
static void finish_charging(int sock, int reason)
{
    sock_ctx_t *ctx = &g_sock[sock];

    /* 1. 关继电器 (先断电, 安全第一) */
    relay_off(sock);

    /* 2. LED 回空闲模式 */
    led_board->set_sock(sock, SOCK_MODE_IDLE);

    /* 3. 上报充电结束事件 (放入 message 事件池, 可靠重试) */
    {
        msg_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = MSG_EVENT_SOCK;
        ev.sock = (uint8_t)(sock + 1);  /* 1-based for server */
        strncpy(ev.ddh, ctx->ddh, sizeof(ev.ddh) - 1);
        ev.val  = reason;

        /* 最多重试 5 次, 间隔 5000ms,
         * 等待服务器回复 "over <ddh>" 来确认 */
        msg_man->add_event(&ev, 5, 5000);
    }

    sysprt->alog("[sock_ctrl] sock=%d: FINISHED (reason=%d, ddh=%s)\r\n",
                 sock + 1, reason, ctx->ddh);

    /* 4. 清空订单上下文, 回到等待新订单 */
    memset(ctx->ddh, 0, sizeof(ctx->ddh));
    ctx->duration_min   = 0;
    ctx->start_tick     = 0;
    ctx->last_pulse     = 0;
    ctx->last_pulse_tick = 0;
    ctx->end_reason     = reason;
    ctx->state          = SOCK_STA_WAIT_ORDER;
}

/* ================================================================
 * 安全检查 — CHARGING 状态专用
 *
 * 每次 work 调用按 CHECK_INTERVAL 频率执行。
 * 按优先级排列, 命中一个就 return (不再检查后续):
 *   1. 时间到
 *   2. 保险丝熔断
 *   3. 无电流 (HLW8012 脉冲停滞)
 *
 * 注意: 手动停止不在安全检查中处理,
 *       而是在 process_orders() 中处理 (新发 queue 消息)。
 * ================================================================ */
static void safety_check(int sock, uint32_t now_tick)
{
    sock_ctx_t *ctx = &g_sock[sock];
    uint32_t elapsed_ms;
    uint32_t pulse_now;

    /* 计算已充电时间 */
    elapsed_ms = (now_tick - ctx->start_tick) * portTICK_PERIOD_MS;

    /* ---- 1. 时间到 ---- */
    if (elapsed_ms >= ctx->duration_min * 60 * 1000) {
        sysprt->alog("[sock_ctrl] sock=%d: time up (%lu/%lu min)\r\n",
                     sock + 1,
                     elapsed_ms / 60000,
                     ctx->duration_min);
        finish_charging(sock, SOCK_END_TIME_UP);
        return;
    }

    /* ---- 2. 保险丝熔断 ---- */
    if (drv_fuse->is_err()) {
        sysprt->alog("[sock_ctrl] sock=%d: fuse blown!\r\n", sock + 1);
        finish_charging(sock, SOCK_END_FUSE);
        return;
    }

    /* ---- 3. 无电流 (HLW8012 脉冲增长检测) ----
     *
     * 充电开始后前 NO_CURRENT_DELAY 秒不检测
     * (给充电器握手时间)。
     * 之后, 每 PULSE_CHECK_MIN 秒比一次脉冲数:
     *   有增长 → 更新 last_pulse / last_pulse_tick
     *   无增长 & 距上次增长 > NO_CURRENT_WINDOW → 判无电流 */
    if (elapsed_ms > NO_CURRENT_DELAY) {
        uint32_t pulse_check_interval =
            (now_tick - ctx->last_pulse_tick) * portTICK_PERIOD_MS;

        if (pulse_check_interval >= PULSE_CHECK_MIN) {
            pulse_now = drv_hlw8012->get_pulse(sock);

            if (pulse_now > ctx->last_pulse) {
                /* 有脉冲增长 → 更新快照 */
                ctx->last_pulse      = pulse_now;
                ctx->last_pulse_tick = now_tick;
            } else {
                /* 无增长 → 检查是否超过窗口 */
                if (pulse_check_interval > NO_CURRENT_WINDOW) {
                    sysprt->alog("[sock_ctrl] sock=%d: no current "
                                 "(pulse stalled %lums)\r\n",
                                 sock + 1, pulse_check_interval);
                    finish_charging(sock, SOCK_END_NO_CURRENT);
                    return;
                }
            }
        }
    }
}

/* ================================================================
 * 处理队列中的订单
 *
 * 每次 work 调用时把队列中所有待处理订单一次性取出处理。
 * 非阻塞 — 队列空则立即返回。
 * ================================================================ */
static void process_orders(void)
{
    sock_order_t order;
    sock_ctx_t  *ctx;

    while (xQueueReceive(g_order_queue, &order, 0) == pdPASS) {
        if (order.sock < 0 || order.sock >= SOCK_MAX) {
            continue;
        }

        ctx = &g_sock[order.sock];

        if (order.type == SOCK_ORDER_START) {
            /* ---- 开始充电 ---- */
            if (ctx->state == SOCK_STA_WAIT_ORDER) {
                uint32_t now_tick = xTaskGetTickCount();

                /* 填充订单信息 */
                strncpy(ctx->ddh, order.ddh, sizeof(ctx->ddh) - 1);
                ctx->ddh[sizeof(ctx->ddh) - 1] = '\0';
                ctx->duration_min = (order.minutes > 0) ? order.minutes : 60;
                ctx->start_tick   = now_tick;

                /* 记录初始脉冲值 (用于后续无电流检测) */
                ctx->last_pulse      = drv_hlw8012->get_pulse(order.sock);
                ctx->last_pulse_tick = now_tick;
                ctx->last_check_tick = now_tick;

                /* 硬件动作: 开继电器 + 启动充电动画 */
                relay_on(order.sock);
                led_board->set_sock(order.sock, SOCK_MODE_CHARGING);

                ctx->state = SOCK_STA_CHARGING;

                sysprt->alog("[sock_ctrl] sock=%d: START → CHARGING\r\n",
                             order.sock + 1);
                sysprt->alog("[sock_ctrl]   ddh=%s, duration=%lumin\r\n",
                             ctx->ddh, ctx->duration_min);
            } else {
                sysprt->alog("[sock_ctrl] sock=%d: busy (state=%d), "
                             "ignore START\r\n",
                             order.sock + 1, ctx->state);
            }

        } else if (order.type == SOCK_ORDER_STOP) {
            /* ---- 停止充电 ---- */
            if (ctx->state == SOCK_STA_CHARGING) {
                sysprt->alog("[sock_ctrl] sock=%d: STOP → FINISHED\r\n",
                             order.sock + 1);
                finish_charging(order.sock, SOCK_END_MANUAL);
            } else {
                sysprt->alog("[sock_ctrl] sock=%d: not charging "
                             "(state=%d), ignore STOP\r\n",
                             order.sock + 1, ctx->state);
            }
        }
    }
}

/* ================================================================
 * do_init — 初始化
 *
 * 创建订单队列 + 关所有继电器 + LED 全灭。
 * 在 task_ctrl 入口调用一次。
 * ================================================================ */
static void do_init(void)
{
    int i;

    /* 创建 FreeRTOS 订单队列 */
    g_order_queue = xQueueCreate(ORDER_QUEUE_LEN, sizeof(sock_order_t));
    if (g_order_queue == NULL) {
        sysprt->aerr("[sock_ctrl] xQueueCreate failed!\r\n");
    }

    /* 逐个插座初始化 */
    for (i = 0; i < SOCK_MAX; i++) {
        memset(&g_sock[i], 0, sizeof(sock_ctx_t));
        relay_off(i);
        led_board->set_sock(i, SOCK_MODE_IDLE);
        g_sock[i].state = SOCK_STA_WAIT_ORDER;
    }

    sysprt->alog("[sock_ctrl] init done: %d sockets, queue=%p\r\n",
                 SOCK_MAX, (void *)g_order_queue);
}

/* ================================================================
 * do_work — 主循环
 *
 * 由 task_ctrl 反复调用 (约每 100ms 一次)。
 * 每次调用做两件事:
 *   1. process_orders:  从队列取出所有新订单并处理
 *   2. safety_check:    对 CHARGING 中的插座做安全检查
 * ================================================================ */
static void do_work(void)
{
    int i;
    uint32_t now = xTaskGetTickCount();

    /* 1. 处理新到的订单 (如果有队列则处理) */
    if (g_order_queue != NULL) {
        process_orders();
    }

    /* 2. 驱动每个插座 */
    for (i = 0; i < SOCK_MAX; i++) {
        sock_ctx_t *ctx = &g_sock[i];

        switch (ctx->state) {

        case SOCK_STA_INIT:
            /* 兜底: INIT 应在 do_init 中被置为 WAIT_ORDER */
            relay_off(i);
            ctx->state = SOCK_STA_WAIT_ORDER;
            break;

        case SOCK_STA_WAIT_ORDER:
            /* 空闲 — 等待队列中的 START 订单 (process_orders 处理) */
            break;

        case SOCK_STA_CHARGING:
            /* 充电中 — 定期安全检查 */
            if ((now - ctx->last_check_tick) * portTICK_PERIOD_MS
                >= CHECK_INTERVAL) {
                safety_check(i, now);
                ctx->last_check_tick = now;
            }
            break;

        case SOCK_STA_FINISHED:
            /* FINISHED 在 finish_charging() 中已转为 WAIT_ORDER,
             * 不应到达此处 */
            ctx->state = SOCK_STA_WAIT_ORDER;
            break;

        default:
            ctx->state = SOCK_STA_WAIT_ORDER;
            break;
        }
    }
}

/* ---- 查询是否正在充电 ---- */
static int do_is_charging(int sock)
{
    if (sock < 0 || sock >= SOCK_MAX) return 0;
    return (g_sock[sock].state == SOCK_STA_CHARGING) ? 1 : 0;
}

/* ---- 查询当前状态 ---- */
static int do_get_state(int sock)
{
    if (sock < 0 || sock >= SOCK_MAX) return -1;
    return g_sock[sock].state;
}

/* ---- 发送订单 (message 层调用, 跨任务安全) ---- */
static int do_send_order(sock_order_t *order)
{
    if (g_order_queue == NULL) return -1;
    if (order == NULL) return -1;
    if (order->sock < 0 || order->sock >= SOCK_MAX) return -1;

    /* 非阻塞发送 — 队列满则丢弃并报错 */
    if (xQueueSend(g_order_queue, order, 0) != pdPASS) {
        sysprt->aerr("[sock_ctrl] queue full, order dropped!\r\n");
        return -1;
    }

    return 0;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static sock_ctrl_t m_sock_ctrl = {
    do_init,
    do_work,
    do_send_order,
    do_is_charging,
    do_get_state,
};

sock_ctrl_pt sock_ctrl = &m_sock_ctrl;
