/**
 * task_ctrl.c — 控制任务
 *
 * 职责:
 *   - 插座充电控制状态机 (sock_ctrl)
 *   - OTA 升级处理 (Step 26, 占位)
 *   - 看门狗喂狗 (Step 28, 占位)
 *
 * 主循环每 100ms 调用 sock_ctrl->work() 一次:
 *   1. 从队列取出 message 层发来的 on/off 订单
 *   2. 推进每个插座的状态机 (WAIT_ORDER → CHARGING → FINISHED)
 *   3. CHARGING 期间做安全检查 (时间/保险丝/无电流)
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sock_ctrl.h"
#include "sysprt.h"

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
 * task_entry — 控制任务入口
 *
 * 1. 初始化 sock_ctrl (关继电器 + 创建订单队列)
 * 2. 主循环: 驱动充电状态机
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    sysprt->alog("[ctrl] starting...\r\n");

    /* 初始化充电控制模块 */
    sock_ctrl->init();

    sysprt->alog("[ctrl] running — sock_ctrl active\r\n");

    while (1) {
        /* 1. 充电控制状态机 (每 100ms 驱动一次) */
        sock_ctrl->work();

        /*
         * 2. 后续:
         *   deal_ota_work();     — OTA 升级处理 (Step 26)
         *   wdt_feed();          — 看门狗喂狗 (Step 28)
         */

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_ctrl);
