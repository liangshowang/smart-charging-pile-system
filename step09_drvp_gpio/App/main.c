/**
 * Step 9: GPIO 端口驱动层
 *
 * 目标: 创建接口指针模式的 GPIO 端口驱动,
 *       上层代码通过 drvp_gpio->write() 操作硬件。
 *
 * 任务:
 *   blink_task  (优先级2): 500ms 翻转 LED (通过 drvp_gpio)
 *   cmd_task    (优先级1): 命令行交互 (led/relay 命令已迁移到 drvp_gpio)
 *
 * 驱动架构:
 *   drvp_gpio (全局指针) → { read, write, set_mode, ... }
 *     → get_pin(pin) → 查 swm32_pin_map[] → SDK 函数
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "drv_uart0.h"
#include "io_config.h"
#include "cmd_line.h"
#include "drvp_gpio.h"

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
        /* 通过接口指针翻转 LED */
        led_state = !led_state;
        drvp_gpio->write(PIN_SLED, led_state ? PIN_HIGH : PIN_LOW);

        /* 每秒发一次队列消息 */
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

        /* 检查队列 (非阻塞) */
        {
            uint32_t ulTick;
            if (xQueueReceive(xTickQueue, &ulTick, 0) == pdTRUE) {
                /* 收到 blink 消息, 调度正常 */
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
    printf("  Step 9: GPIO Port Driver (drvp)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  GPIO API  : drvp_gpio->read/write/\r\n");
    printf("              set_mode/attach_irq/...\r\n");
    printf("  Pins      : 64-pin map (SWM320RET7)\r\n");
    printf("========================================\r\n");

    /* 创建队列 */
    xTickQueue = xQueueCreate(10, sizeof(uint32_t));

    /* 创建任务 */
    xTaskCreate(vBlinkTask, "blink", 128, NULL, 2, &xBlinkTaskHandle);
    xTaskCreate(vCmdTask,   "cmd",   512, NULL, 1, &xCmdTaskHandle);

    /* 启动调度器 */
    vTaskStartScheduler();

    while (1);
}
