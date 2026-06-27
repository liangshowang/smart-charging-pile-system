/**
 * Step 10: 驱动层 — GPIO 透传 + 环形缓冲抽象
 *
 * 目标: 在 drvp (端口层) 上建立 drv (驱动层),
 *       GPIO 透传 + 环形缓冲统一 API。
 *
 * 三层架构 (以 GPIO 写为例):
 *   drv_gpio->write(pin, val)     ← 应用层调用 (drv)
 *     → drvp_drv_gpio->write()    ← 端口层 (drvp)
 *       → GPIO_SetBit()       ← SDK
 *
 * 任务:
 *   blink_task  (优先级2): 500ms 翻转 LED (通过 drv_gpio->write)
 *   cmd_task    (优先级1): 命令行交互 (已全面迁移到 gpio + loop)
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
    printf("  Step 10: Driver Layer (drv)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  GPIO API  : drv_gpio->write (drv)\r\n");
    printf("              → drvp_gpio → SDK\r\n");
    printf("  Loop  API : loop->create/read/write\r\n");
    printf("  Loop Size : 1024 bytes\r\n");
    printf("========================================\r\n");

    xTickQueue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(vBlinkTask, "blink", 128, NULL, 2, &xBlinkTaskHandle);
    xTaskCreate(vCmdTask,   "cmd",   512, NULL, 1, &xCmdTaskHandle);

    vTaskStartScheduler();

    while (1);
}
