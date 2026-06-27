/**
 * task_start.c — BL 主任务
 *
 * 职责:
 *   - 初始化 UART1 (4G 模块)
 *   - 创建 air724 设备
 *   - 驱动 OTA 状态机
 *
 * 与 APP 的 task_start 不同, BL 版本更轻量:
 *   无 flashdb, sock_ctrl, calendar, hlw8012, fuse, net_man
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "flash_partition.h"
#include "../boot_config.h"
#include "ota_state.h"
#include "sysprt.h"
#include "at_dev.h"
#include "air_man.h"
#include "drv_uart0.h"
#include "drv_uart1.h"
#include "drv_gpio.h"
#include "io_config.h"
#include <stdio.h>
#include <string.h>

/* ---- 任务句柄 ---- */
static TaskHandle_t g_task_handle = NULL;

/* ---- 前向声明 ---- */
static void task_entry(void *arg);

/* ================================================================
 * create_task_start — 创建启动任务 (由 main.c 调用)
 * ================================================================ */
void create_task_start(void)
{
    xTaskCreate(task_entry,
                "start",
                2048,       /* 栈大小 2KB */
                NULL,
                2,          /* 优先级 */
                &g_task_handle);

    sysprt->alog("[bl] task_start created\r\n");
}

/* ================================================================
 * task_entry — BL 主任务入口
 * ================================================================ */
static void task_entry(void *arg)
{
    boot_config_t cfg;
    int ret;

    (void)arg;

    /* ---- 0. 初始化 UART0 和日志 (最早, printf/sysprt 依赖) ---- */
    uart0->init(115200);
    sysprt->init();

    sysprt->alog("[bl] ===========================\r\n");
    sysprt->alog("[bl] BootLoader v2.0 (OTA mode)\r\n");
    sysprt->alog("[bl] ===========================\r\n");

    /* ---- 1. 读启动配置 ---- */
    ret = boot_config_read(&cfg);
    if (ret == 0) {
        sysprt->alog("[bl] CONF: boot_code=0x%08lX boot_app=%lu "
                     "ota_req=%lu net_mode=%lu\r\n",
                     cfg.boot_code, cfg.boot_app,
                     cfg.ota_req, cfg.net_mode);
    } else {
        sysprt->alog("[bl] CONF not initialized, using defaults\r\n");
    }

    /* ---- 2. GPIO 初始化 ---- */
    io_init_all();

    /* 输出引脚初始电平 */
    drv_gpio->write(PIN_ELC0, PIN_LOW);
    drv_gpio->write(PIN_ELC1, PIN_LOW);
    drv_gpio->write(PIN_SLED, PIN_LOW);
    drv_gpio->write(PIN_NETRST, PIN_HIGH);   /* 4G 正常运行 */

    /* ---- 3. UART1 初始化 (4G 模块) ---- */
    uart1->init(115200);
    sysprt->alog("[bl] UART1 initialized (115200)\r\n");

    /* ---- 4. 创建 4G 设备 ---- */
    g_dev = air_man->create_dev(uart1);
    if (g_dev == NULL) {
        sysprt->aerr("[bl] air724 create FAILED!\r\n");
        /* 死循环 — 等待看门狗复位 */
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    sysprt->alog("[bl] air724 device created\r\n");

    /* ---- 5. 初始化 OTA 状态机 ---- */
    ota_state_init();

    /* ---- 6. 主循环: 驱动 OTA 状态机 ---- */
    while (1) {
        int ota_ret = ota_state_work();

        if (ota_ret < 0) {
            /* OTA 出错 — 打印错误并等待看门狗复位 */
            sysprt->aerr("[bl] OTA FATAL ERROR, phase=%d\r\n",
                         ota_state_get_phase());
            vTaskDelay(pdMS_TO_TICKS(5000));
            /* 尝试重新开始 */
            ota_state_init();
        }

        /* 让出 CPU */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ---- 全局设备指针 (供 ota_state 使用) ---- */
air_dev_pt g_dev = NULL;
