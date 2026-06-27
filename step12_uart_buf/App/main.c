/**
 * Step 12: 驱动层 — UART 双环形缓冲抽象
 *
 * 目标: 为 UART0 建立双环形缓冲 (TX+RX), 实现非阻塞发送,
 *       并提供 uart0 接口指针统一 UART 操作。
 *
 * 完整四层架构 (TX 发送为例):
 *   printf → fputc → uart0->send_byte()  ← 应用层
 *     → TX loopbuf                        ← 驱动层 (环形缓冲)
 *       → tx_callback → drvp_uart0 ISR    ← 端口层 (硬件)
 *         → UART_WriteByte()              ← SDK
 *
 * 任务:
 *   blink_task  (优先级2): 500ms 翻转 LED
 *   cmd_task    (优先级1): 命令行交互
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "drv_uart0.h"
#include "io_config.h"
#include "cmd_line.h"
#include "drv_gpio.h"

#define PIN_SLED  17   /* C4 */

/* ================================================================
 * 任务句柄
 * ================================================================ */
static TaskHandle_t xBlinkTaskHandle = NULL;
static TaskHandle_t xCmdTaskHandle   = NULL;

/* ================================================================
 * 队列句柄
 * ================================================================ */
static QueueHandle_t xTickQueue = NULL;

/* ================================================================
 * blink_task — 500ms 翻转 LED
 * ================================================================ */
static void vBlinkTask(void *pvParameters)
{
    TickType_t xLastWakeTime;
    uint32_t   ulBlinkCount = 0;
    int        led_state = 0;

    (void)pvParameters;
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        led_state = !led_state;
        drv_gpio->write(PIN_SLED, led_state ? PIN_HIGH : PIN_LOW);

        ulBlinkCount++;
        if ((ulBlinkCount & 1) == 0) {
            uint32_t ulTick = xTaskGetTickCount();
            xQueueSend(xTickQueue, &ulTick, 0);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

/* ================================================================
 * cmd_task — 命令行交互
 * ================================================================ */
static void vCmdTask(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        cmd_line_work();

        {
            uint32_t ulTick;
            if (xQueueReceive(xTickQueue, &ulTick, 0) == pdTRUE) {
                /* 队列通信正常 */
            }
        }
    }
}

/* ================================================================
 * main
 * ================================================================ */
int main(void)
{
    SystemInit();

    io_init_all();
    uart0->init(115200);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 12: Dual Ring Buffer UART\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  TX Buffer : 1024 bytes (ISR-driven)\r\n");
    printf("  RX Buffer : 1024 bytes (ISR-driven)\r\n");
    printf("  API       : uart0->send/read_rx_buf\r\n");
    printf("========================================\r\n");

    xTickQueue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(vBlinkTask, "blink", 128, NULL, 2, &xBlinkTaskHandle);
    xTaskCreate(vCmdTask,   "cmd",   512, NULL, 1, &xCmdTaskHandle);

    vTaskStartScheduler();

    while (1);
}
