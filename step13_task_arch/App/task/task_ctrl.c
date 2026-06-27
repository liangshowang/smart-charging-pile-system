/**
 * task_ctrl.c — 控制任务
 *
 * 职责 (后续步骤填充):
 *   - 插座充电控制状态机 (Step 23)
 *   - OTA 升级处理 (Step 26)
 *   - 看门狗喂狗 (Step 28)
 *
 * 当前状态: 占位, 仅打印启动信息 + 空循环。
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_uart0.h"

__task_body_start;

/* ---- 任务元数据 ---- */
static task_body_t m_task_body = {
    .name       = "ctrl_task",
    .cn_name    = "控制任务",
    .stack_size = 2048,
    .priority   = 3,
    .handle     = &handle,
    .create     = create,
};

/* ================================================================
 * task_entry — 控制任务入口 (占位)
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    printf("[ctrl] running (placeholder)\r\n");

    while (1) {
        /*
         * 后续:
         *   sock_ctrl_work();     — 充电控制状态机
         *   deal_ota_work();     — OTA 升级处理
         *   wdt_feed();          — 看门狗喂狗
         */
        vTaskDelay(100);
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_ctrl);
