/**
 * task_host.c — 主控任务
 *
 * 职责:
 *   - LED 状态刷新 (500ms 翻转, 替代 step12 的 blink_task)
 *   - 命令行交互 (替代 step12 的 cmd_task)
 *   - 后续扩展: LED 灯板动画 / 日志输出 (sysprt->work)
 *
 * 同一循环内处理:
 *   1. 检查 500ms 是否到期 → 翻转 LED
 *   2. 调用 cmd_line_work() → 处理串口输入
 *   3. vTaskDelay(2) 让出 CPU
 *
 * 依赖:
 *   drv_gpio  — GPIO 驱动 (drv_gpio->write)
 *   drv_uart0 — UART 双缓冲 (cmd_line_work 内部使用 uart0->read_rx_buf)
 *   cmd_line  — 命令行状态机
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_uart0.h"
#include "drv_gpio.h"
#include "io_config.h"
#include "cmd_line.h"

__task_body_start;

/* ---- 任务元数据 ---- */
static task_body_t m_task_body = {
    .name       = "host_task",
    .cn_name    = "主控任务",
    .stack_size = 4096,
    .priority   = 3,
    .handle     = &handle,
    .create     = create,
};

/* ================================================================
 * task_entry — 主控任务入口
 *
 * 单循环处理:
 *   - LED 500ms 翻转 (基于 Tick 的非阻塞计时)
 *   - 命令行轮询 (cmd_line_work 是状态机, 无输入时立即返回)
 * ================================================================ */
static void task_entry(void *arg)
{
    TickType_t xLastWakeTime;
    int        led_state = 0;

    (void)arg;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* ---- LED 500ms 翻转 ---- */
        {
            TickType_t xNow = xTaskGetTickCount();
            if ((xNow - xLastWakeTime) >= pdMS_TO_TICKS(500)) {
                xLastWakeTime = xNow;
                led_state = !led_state;
                drv_gpio->write(PIN_SLED, led_state ? PIN_HIGH : PIN_LOW);
            }
        }

        /* ---- 命令行交互 (非阻塞状态机) ---- */
        cmd_line_work();

        /* ---- 后续扩展点 ---- */
        /* led_board->work();    — Step 15 HC595 灯板 */
        /* sysprt->work();       — Step 14 日志系统 */

        vTaskDelay(2);
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_host);
