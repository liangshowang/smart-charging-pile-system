/**
 * sysprt.c — 线程安全日志系统实现
 *
 * 核心机制:
 *   1. FreeRTOS 消息队列 (100 条 × 128 字节)
 *   2. 日志调用 → vsnprintf 格式化 → xQueueSend 入队
 *   3. sysprt->work() → xQueueReceive 出队一条 → printf 输出
 *
 * ISR 安全:
 *   irq_log 使用 xQueueSendFromISR, 配合 taskENTER_CRITICAL_FROM_ISR
 *   确保在中断上下文中安全入队。
 *
 * 队列满时: 静默丢弃 (不阻塞调用者)
 */

#include "sysprt.h"
#include "SWM320.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- 消息队列句柄 ---- */
static QueueHandle_t g_prt_mq = NULL;

/* ---- 颜色码 (可选的终端着色) ---- */
#define CLR_NONE   ""
#define CLR_WHITE  ""
#define CLR_GREEN  ""
#define CLR_RED    ""
#define CLR_RESET  ""

/* ================================================================
 * do_send — 通用发送 (任务上下文)
 *
 * 格式化消息 → 填入 pmsg_t → 发送到队列
 * 队列满时静默丢弃 (send_ok = 0 被忽略)
 * ================================================================ */
static void do_send(uint8_t type, uint8_t use_tick,
                   const char *format, va_list args)
{
    pmsg_t msg;

    if (g_prt_mq == NULL)
        return;

    msg.type     = type;
    msg.use_tick = use_tick;
    memset(msg.reserve, 0, sizeof(msg.reserve));

    /* 格式化到消息缓冲 */
    vsnprintf(msg.buf, SYSPRT_BUF_SIZE, format, args);
    msg.buf[SYSPRT_BUF_SIZE - 1] = '\0';

    /* 非阻塞发送 — 队列满就丢弃 */
    BaseType_t send_ok = xQueueSend(g_prt_mq, (void *)&msg, 0);
    (void)send_ok;
}

/* ================================================================
 * do_send_from_isr — ISR 安全发送
 * ================================================================ */
static void do_send_from_isr(uint8_t type, const char *format, va_list args)
{
    pmsg_t msg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (g_prt_mq == NULL)
        return;

    msg.type     = type;
    msg.use_tick = 0;   /* ISR 内不加时间戳 (避免额外耗时) */
    memset(msg.reserve, 0, sizeof(msg.reserve));

    vsnprintf(msg.buf, SYSPRT_BUF_SIZE, format, args);
    msg.buf[SYSPRT_BUF_SIZE - 1] = '\0';

    xQueueSendFromISR(g_prt_mq, (void *)&msg, &xHigherPriorityTaskWoken);

    /* 如果入队唤醒了一个更高优先级的任务, 触发调度 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ================================================================
 * 五级日志包装宏 (宏比函数更合适 — 保持函数签名简单)
 *
 * 每个宏调用 do_send, 第二个参数 use_tick=0 (无时间戳)。
 * 带 t 前缀的版本 use_tick=1。
 * ================================================================ */

/* ---- 无时间戳 ---- */
#define DEF_LOG_FN(name, type) \
    static void sysprt_##name(const char *format, ...) \
    { \
        va_list args; \
        va_start(args, format); \
        do_send(type, 0, format, args); \
        va_end(args); \
    }

DEF_LOG_FN(log,  PTF_STDLOG)
DEF_LOG_FN(dlog, PTF_DRVLOG)
DEF_LOG_FN(alog, PTF_APPLOG)
DEF_LOG_FN(derr, PTF_DRVERR)
DEF_LOG_FN(aerr, PTF_APPERR)

/* ---- ISR 安全日志 ---- */
static void sysprt_irq_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    do_send_from_isr(PTF_STDLOG, format, args);
    va_end(args);
}

/* ================================================================
 * do_init — 初始化消息队列
 * ================================================================ */
static void do_init(void)
{
    if (g_prt_mq != NULL)
        return;

    g_prt_mq = xQueueCreate(SYSPRT_QUEUE_LEN, sizeof(pmsg_t));

    if (g_prt_mq == NULL) {
        /* 内存分配失败 — 系统不应该继续, 此处死循环 */
        while (1);
    }
}

/* ================================================================
 * do_work — 出队并打印一条消息
 *
 * 设计: 每次调用只处理一条, 保证 host 任务的响应性。
 * host_task 每 2ms 调用一次, 吞吐量 500 条/秒, 远超实际需求。
 *
 * 被丢弃时: 队列为空 → 直接返回, 无输出。
 * ================================================================ */
static void do_work(void)
{
    pmsg_t msg;
    const char *color_start = CLR_NONE;
    const char *color_end   = CLR_RESET;

    if (g_prt_mq == NULL)
        return;

    /* 非阻塞接收 — 队列空则返回 pdFALSE */
    if (xQueueReceive(g_prt_mq, (void *)&msg, 0) != pdTRUE)
        return;

    /* 选颜色 */
    switch (msg.type) {
    case PTF_DRVLOG:  color_start = CLR_GREEN; break;
    case PTF_DRVERR:
    case PTF_APPERR:  color_start = CLR_RED;   break;
    case PTF_APPLOG:  color_start = CLR_WHITE;  break;
    default:          color_start = CLR_NONE;  break;
    }

    /* 输出: [tick]颜色 + 消息 + 复位 */
    if (msg.use_tick) {
        printf("%s[%d] %s%s", color_start, (int)xTaskGetTickCount(),
               msg.buf, color_end);
    } else {
        printf("%s%s%s", color_start, msg.buf, color_end);
    }
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static sysprt_t m_sysprt = {
    do_init,
    do_work,
    sysprt_log,
    sysprt_dlog,
    sysprt_alog,
    sysprt_derr,
    sysprt_aerr,
    sysprt_irq_log,
};

sysprt_pt sysprt = &m_sysprt;
