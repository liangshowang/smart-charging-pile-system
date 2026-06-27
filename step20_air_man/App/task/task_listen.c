/**
 * task_listen.c — 监听任务
 *
 * 职责:
 *   - 保险丝检测 (光耦 50Hz 脉冲计数, 5ms 轮询)
 *   - HLW8012 计量芯片脉冲统计轮询
 *
 * 优先级 5 (高于 start/ctrl/host): 确保脉冲不漏数。
 *
 * 本任务循环每 5ms 执行一次:
 *   1. drv_fuse->work()     — 保险丝脉冲计数 + 1s 判定
 *   2. drv_hlw8012->work()  — 计量脉冲统计 + 5s/1min 上报
 *   3. led_board->set_fuse() — 控制保险丝 LED
 *
 * 依赖:
 *   drv_fuse    — 保险丝检测 (GPIO 轮询)
 *   drv_hlw8012 — HLW8012 计量驱动 (GPIO 中断)
 *   led_board   — LED 灯板控制
 *   sysprt      — 日志输出
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_fuse.h"
#include "drv_hlw8012.h"
#include "led_board.h"
#include "sysprt.h"

__task_body_start;

/* ---- 任务元数据 ---- */
static task_body_t m_task_body = {
    .name       = "listen_task",
    .cn_name    = "监听任务",
    .stack_size = 1024,
    .priority   = 5,
    .handle     = &handle,
    .create     = create,
};

/* ================================================================
 * task_entry — 监听任务入口
 *
 * 循环每 5ms 执行一次:
 *   1. 保险丝检测 (内部有 5ms 去抖 + 1s 判定)
 *   2. HLW8012 计量轮询 (内部有 5s/1min 周期)
 *   3. 保险丝 LED 控制
 *
 * 注意:
 *   - HLW8012 实际脉冲采集在 GPIO ISR 中完成
 *   - 保险丝检测在 5ms 轮询中完成, 无中断开销
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    sysprt->alog("[listen] fuse + HLW8012 metering running\r\n");

    while (1) {
        /* 1. 保险丝检测: 5ms 去抖 + 1s 判定 (<10 pulse = 异常) */
        drv_fuse->work();

        /* 2. 计量轮询: 拔枪检测 + 5s 上报 + 1min 上报 */
        drv_hlw8012->work();

        /* 3. 保险丝 LED: 正常=亮, 熔断=灭 */
        led_board->set_fuse(drv_fuse->is_err() ? 0 : 1);

        /* 5ms 采样周期 (保险丝 50Hz 检测需要快速轮询) */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_listen);
