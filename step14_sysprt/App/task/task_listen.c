/**
 * task_listen.c — 监听任务
 *
 * 职责 (后续步骤填充):
 *   - HLW8012 计量芯片快速轮询 (Step 16)
 *   - 实时读取电压/电流/功率脉冲
 *
 * 优先级 5 (高于 start/ctrl/host): 确保脉冲不漏数。
 *
 * 当前状态: 占位, 仅打印启动信息 + 空循环。
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
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
 * task_entry — 监听任务入口 (占位)
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    sysprt->alog("[listen] running (placeholder)\r\n");

    while (1) {
        /*
         * 后续:
         *   sock_hlw_opt->listen_work();  — HLW8012 脉冲轮询
         *   coder->work();                — 编码器轮询
         */
        vTaskDelay(100);
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_listen);
