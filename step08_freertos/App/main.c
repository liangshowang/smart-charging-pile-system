/**
 * Step 8: FreeRTOS 多任务
 *
 * 目标: 移植 FreeRTOS, 两个任务并行运行, 通过队列通信。
 *
 * 任务:
 *   blink_task  (优先级2, 128W栈): 500ms 翻转 LED
 *   cmd_task    (优先级1, 512W栈): 命令行交互
 *
 * FreeRTOS 接管:
 *   - SysTick_Handler (tick 1KHz, 由 port.c 实现)
 *   - PendSV_Handler  (上下文切换)
 *   - SVC_Handler     (启动调度器)
 *
 * 数据流:
 *   UART RX ISR → 环形缓冲 → cmd_task (poll) → 命令执行
 *                   ↑ 不受 FreeRTOS 影响 (ISR 不调 API)
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "drv_uart0.h"
#include "io_config.h"
#include "cmd_line.h"

/* ================================================================
 * 任务句柄
 * ================================================================ */
static TaskHandle_t xBlinkTaskHandle = NULL;
static TaskHandle_t xCmdTaskHandle   = NULL;

/* ================================================================
 * 队列句柄 (演示用: blink 每秒发送 tick 值给 cmd)
 * ================================================================ */
static QueueHandle_t xTickQueue = NULL;

/* ================================================================
 * blink_task — 500ms 翻转 LED, 每秒发一次队列消息
 * ================================================================ */
static void vBlinkTask(void *pvParameters)
{
    TickType_t xLastWakeTime;
    uint32_t   ulBlinkCount = 0;

    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* 翻转 LED */
        GPIO_InvBit(GPIOC, PIN4);

        /* 每两次翻转 (即1秒) 发送一次队列消息 */
        ulBlinkCount++;
        if ((ulBlinkCount & 1) == 0) {
            uint32_t ulTick = xTaskGetTickCount();
            xQueueSend(xTickQueue, &ulTick, 0);
        }

        /* 精确 500ms 周期 */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

/* ================================================================
 * cmd_task — 命令行交互 (不休眠, 让调度器抢占)
 * ================================================================ */
static void vCmdTask(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        cmd_line_work();

        /* 检查队列 (非阻塞) */
        {
            uint32_t ulTick;
            if (xQueueReceive(xTickQueue, &ulTick, 0) == pdTRUE) {
                /* 收到 blink 发来的 tick, 说明调度正常 */
            }
        }
    }
}

/* ================================================================
 * main — 硬件初始化 → 创建任务 → 启动调度器
 * ================================================================ */
int main(void)
{
    SystemInit();

    io_init_all();
    UART0_Init(115200);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 8: FreeRTOS Multi-Task\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  Tick      : 1000 Hz\r\n");
    printf("  Tasks     : blink(prio2, 500ms)\r\n");
    printf("              cmd  (prio1, poll)\r\n");
    printf("  Queue     : 10 x uint32\r\n");
    printf("  Heap      : 90 KB (heap_4)\r\n");
    printf("========================================\r\n");

    /* 创建队列 */
    xTickQueue = xQueueCreate(10, sizeof(uint32_t));

    /* 创建任务 */
    xTaskCreate(vBlinkTask, "blink", 128, NULL, 2, &xBlinkTaskHandle);
    xTaskCreate(vCmdTask,   "cmd",   512, NULL, 1, &xCmdTaskHandle);

    /* 启动调度器 — 从此不再返回 */
    vTaskStartScheduler();

    /* 永远不会到这里 */
    while (1);
}
