/**
 * Step 11: 端口驱动 — UART0
 *
 * 目标: 为 UART0 建立 drvp 端口层, 补齐三层架构的最后一块。
 *
 * 完整三层架构:
 *   drv_gpio->write(pin, val)     ← 驱动层 (drv)
 *     → drvp_gpio->write()        ← 端口层 (drvp)
 *       → GPIO_SetBit()           ← SDK
 *
 *   loop->write(g_uart0_loop, b)  ← 驱动层 (环形缓冲)
 *
 *   UART0_Init → drvp_uart0->Init() ← 端口层 (硬件)
 *   ISR (drvp_uart0) → rx_callback → loop->write()
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
 * blink_task — 500ms 翻转 LED (通过驱动层 gpio)
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
    UART0_Init(115200);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 11: UART Port Driver (drvp)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  UART API  : drvp_uart0->Init/Write/Open\r\n");
    printf("              -> SDK UART_Init/WriteByte/...\r\n");
    printf("  UART ISR  : drvp_uart0 ISR -> rx_callback\r\n");
    printf("              -> loop->write(g_uart0_loop)\r\n");
    printf("========================================\r\n");

    xTickQueue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(vBlinkTask, "blink", 128, NULL, 2, &xBlinkTaskHandle);
    xTaskCreate(vCmdTask,   "cmd",   512, NULL, 1, &xCmdTaskHandle);

    vTaskStartScheduler();

    while (1);
}
