/**
 * task_listen.c — 监听任务
 *
 * 职责:
 *   - HLW8012 计量芯片脉冲计数轮询
 *   - 实时检测功率/电流/电压变化
 *   - 拔枪检测 (无脉冲超时)
 *
 * 优先级 5 (高于 start/ctrl/host): 确保脉冲不漏数。
 * 本任务延迟很短 (100ms), 每次调用 hlw8012->work() 处理
 * 脉冲统计和周期上报。
 *
 * 依赖:
 *   drv_hlw8012 — HLW8012 驱动 (GPIO 中断脉冲计数)
 *   sysprt       — 日志输出
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_hlw8012.h"
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
 * 循环每 100ms 执行一次:
 *   hlw8012->work() 负责:
 *     - 拔枪检测 (3 秒无脉冲 → 报警)
 *     - 每 5 秒输出脉冲计数 (供上层计算瞬时功率)
 *     - 每 60 秒输出分钟累计 (供上层计算分钟电量)
 *
 * 注意: 实际脉冲采集在 GPIO 中断 ISR 中完成,
 *       本任务只做"批量处理 + 周期上报"。
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    sysprt->alog("[listen] HLW8012 metering running\r\n");

    while (1) {
        /* 计量轮询: 脉冲统计 + 拔枪检测 + 周期上报 */
        drv_hlw8012->work();

        /* 100ms 周期: 足够检测 3 秒无脉冲事件 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_listen);
