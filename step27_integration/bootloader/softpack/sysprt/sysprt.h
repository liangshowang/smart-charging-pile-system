/**
 * sysprt.h — 线程安全日志系统接口
 *
 * 通过 FreeRTOS 消息队列解耦日志生成和 UART 输出:
 *   多任务/ISR → sysprt->alog(...) → 消息队列 → sysprt->work() → printf
 *
 * 解决了裸 printf 的三个问题:
 *   1. 多任务 printf 输出交错  → 队列串行化, 一条消息原子输出
 *   2. ISR 不能 printf         → irq_log 用 xQueueSendFromISR
 *   3. printf 阻塞 TX 缓冲     → 生成日志不阻塞, work 后台慢慢发
 *
 * 日志级别:
 *   log  — 标准日志
 *   dlog — 驱动日志
 *   alog — 应用日志
 *   derr — 驱动错误
 *   aerr — 应用错误
 *
 * 使用:
 *   sysprt->init();           // 创建消息队列 (只调一次)
 *   sysprt->alog("充电开始, 功率=%dW\r\n", power);
 *   sysprt->work();           // 在主循环中周期性调用, 出队打印
 */

#ifndef __SYSPRT_H__
#define __SYSPRT_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

/* ---- 消息缓冲区大小 ---- */
#define SYSPRT_BUF_SIZE  128

/* ---- 消息队列长度 ---- */
#define SYSPRT_QUEUE_LEN 100

/* ---- 日志级别 ---- */
enum ptf_type {
    PTF_STDLOG = 0,   /* 标准日志 */
    PTF_DRVLOG,       /* 驱动日志 */
    PTF_APPLOG,       /* 应用日志 */
    PTF_DRVERR,       /* 驱动错误 */
    PTF_APPERR,       /* 应用错误 */
};

/* ---- 日志消息 ---- */
typedef struct {
    uint8_t type;                        /* 日志级别 (enum ptf_type) */
    uint8_t use_tick;                    /* 1=带时间戳 */
    uint8_t reserve[2];                  /* 对齐 */
    char    buf[SYSPRT_BUF_SIZE];        /* 格式化后的消息 */
} pmsg_t;

/* ---- 接口 ---- */
typedef struct {
    void (*init)(void);         /* 初始化消息队列 */
    void (*work)(void);         /* 出队一条消息并打印 (周期性调用) */

    /* 任务上下文日志 (使用 xQueueSend) */
    void (*log )(const char *format, ...);   /* 标准日志 */
    void (*dlog)(const char *format, ...);   /* 驱动日志 */
    void (*alog)(const char *format, ...);   /* 应用日志 */
    void (*derr)(const char *format, ...);   /* 驱动错误 */
    void (*aerr)(const char *format, ...);   /* 应用错误 */

    /* ISR 上下文日志 (使用 xQueueSendFromISR) */
    void (*irq_log)(const char *format, ...);
} sysprt_t, *sysprt_pt;

/* ---- 全局接口指针 (sysprt.c 定义) ---- */
extern sysprt_pt sysprt;

#endif /* __SYSPRT_H__ */
