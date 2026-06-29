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
#include "calendar.h"
#include "sysprt.h"
#include "drv_wdt.h"
#include "drv_adc.h"

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

    /* 初始化 RTC + 日历模块 (Step 24) */
    calendar->init();

    /* 初始化充电控制模块 */
    sock_ctrl->init();

    sysprt->alog("[ctrl] running — sock_ctrl + calendar + WDT + ADC active\r\n");

    while (1) {
        /* 1. 刷新 RTC 缓存时间 */
        calendar->work();

        /* 2. 充电控制状态机 (每 100ms 驱动一次) */
        sock_ctrl->work();

        /* 3. 看门狗喂狗 — 防止系统死锁 */
        wdt->feed();

        /* 4. ADC 电压监测 (每 500ms 检查一次) */
        {
            static uint8_t adc_cnt = 0;

            if (++adc_cnt >= 5) {
                adc_cnt = 0;
                uint32_t vbat_mv = adc->read_mv(0, 3300, 11);
                /* 电压异常保护: <180V 或 >260V 告警 */
                if (vbat_mv < 180000 && vbat_mv > 100) {
                    sysprt->aerr("[ctrl] WARNING: low voltage %lu mV\r\n",
                                 vbat_mv);
                } else if (vbat_mv > 260000) {
                    sysprt->aerr("[ctrl] WARNING: over voltage %lu mV\r\n",
                                 vbat_mv);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_ctrl);
