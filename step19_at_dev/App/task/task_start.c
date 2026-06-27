/**
 * task_start.c — 启动任务
 *
 * 职责:
 *   - 上电后第一个运行
 *   - 初始化硬件 (IO / UART0 / UART1)
 *   - 初始化 AT 命令引擎 (4G 模块)
 *   - 打印版本信息
 *   - 依次创建 host → listen → ctrl 任务
 *   - 主循环: AT 引擎测试 (后续 Step 21 填充网络调度)
 *
 * 初始化顺序 (严格):
 *   main() → task_start->create() → task_entry()
 *     → uart0->init()        ← 必须最先! printf 依赖它
 *     → sysprt->init()       ← 创建消息队列 (日志系统就绪)
 *     → io_init_all()        ← 配置 GPIO 模式 (printf 可用)
 *     → init_io_pin()        ← 设置输出初始电平
 *     → uart1->init()        ← 4G 模块 UART (115200)
 *     → at_dev->create()     ← 创建 AT 设备
 *     → show_banner()
 *     → task_host->create()
 *     → task_listen->create()
 *     → task_ctrl->create()
 *     → while(1) { AT test + vTaskDelay; }   // AT 引擎测试
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_uart0.h"
#include "drv_uart1.h"
#include "drv_gpio.h"
#include "io_config.h"
#include "sysprt.h"
#include "led_board.h"
#include "drv_hlw8012.h"
#include "drv_fuse.h"
#include "flashdb.h"
#include "at_dev.h"

__task_body_start;

/* ---- 任务元数据 ---- */
static task_body_t m_task_body = {
    .name       = "start_task",
    .cn_name    = "启动任务",
    .stack_size = 4096,
    .priority   = 3,
    .handle     = &handle,
    .create     = create,
};

/* ================================================================
 * show_banner — 打印版本信息
 * ================================================================ */
static void show_banner(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 19: AT Command Engine (at_dev)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  UART0     : 115200 bps (debug console)\r\n");
    printf("  UART1     : 115200 bps (4G module AIR724)\r\n");
    printf("  AT Engine : send_cmd + auto_baud + timeout\r\n");
    printf("========================================\r\n");
    printf("\r\n");
}

/* ================================================================
 * task_entry — 启动任务入口
 *
 * 执行顺序:
 *   1. UART0 初始化 (115200, 必须最先)
 *   2. sysprt 日志系统初始化
 *   3. GPIO 模式配置
 *   4. 输出引脚初始电平设定
 *   5. LED 灯板初始化
 *   6. HLW8012 计量初始化
 *   7. 保险丝检测初始化
 *   8. FlashDB 配置加载
 *   9. UART1 初始化 (4G 模块, 115200)
 *  10. AT 引擎创建 (绑定 uart1)
 *  11. LED 演示动画
 *  12. 打印版本信息
 *  13. 创建 host 任务
 *  14. 创建 listen 任务
 *  15. 创建 ctrl 任务
 *  16. 主循环: AT 引擎测试
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    /*
     * 初始化顺序严格:
     *   UART → GPIO → 电平设定 → 打印 → 创建任务
     *
     * UART 必须最先初始化, 因为 io_init_all() 和 show_banner()
     * 内部使用 printf, 依赖 uart0 的就绪。
     */

    /* ---- 1. UART0 初始化 (必须最先, printf 依赖) ---- */
    uart0->init(115200);

    /* ---- 2. 日志系统初始化 (创建消息队列, 此后可用 sysprt->alog) ---- */
    sysprt->init();

    /* ---- 3. GPIO 模式配置 (PORT_Init + GPIO_INIT, printf 可用) ---- */
    io_init_all();

    /* ---- 4. 输出引脚初始电平 (继电器断开 / LED 熄灭 / NetRst 高) ---- */
    drv_gpio->write(PIN_ELC0, PIN_LOW);
    drv_gpio->write(PIN_ELC1, PIN_LOW);
    drv_gpio->write(PIN_SLED, PIN_LOW);
    drv_gpio->write(PIN_NETRST, PIN_HIGH);

    /* ---- 5. 灯板初始化 (HC595 + 16 LED 全灭) ---- */
    led_board->init();

    /* ---- 6. HLW8012 计量初始化 (GPIO 中断脉冲计数) ---- */
    drv_hlw8012->init();

    /* ---- 7. 保险丝检测初始化 (GPIO 轮询) ---- */
    drv_fuse->init();

    /* ---- 8. FlashDB 初始化 (从 Flash 加载配置, 首次写默认值) ---- */
    flashdb->init();

    /* ---- 9. UART1 初始化 (4G 模块 AIR724, 115200 bps) ---- */
    uart1->init(115200);

    /* ---- 10. 创建 AT 设备 (绑定 uart1, 用于与 4G 模块通信) ---- */
    air724 = at_man->create(uart1);
    if (air724) {
        sysprt->alog("[task_start] AT device created\r\n");
    }

    /* ---- 11. 演示: 两插座充电跑马灯 ---- */
    led_board->set_sock(SOCK_0, SOCK_MODE_CHARGING);
    led_board->set_sock(SOCK_1, SOCK_MODE_CHARGING);
    led_board->set_net(1);   /* 网络灯亮 */
    sysprt->alog("[led_board] charging animation started\r\n");

    /* ---- 12. 打印版本信息 ---- */
    show_banner();

    /* ---- 13. 创建 host 任务 (LED 刷新 + 命令行 + 灯板动画) ---- */
    task_host->create(NULL, NULL);

    /* ---- 14. 创建 listen 任务 (保险丝检测 + HLW8012 脉冲轮询) ---- */
    task_listen->create(NULL, NULL);

    /* ---- 15. 创建 ctrl 任务 (占位 — 后续充电控制 + OTA) ---- */
    task_ctrl->create(NULL, NULL);

    /* ---- 16. 主循环: AT 引擎测试 ---- */
    while (1) {
        if (air724) {
            int ret;

            /* 测试 1: AT 基础命令 (查询模块是否在线) */
            sysprt->alog("[AT test] sending AT...\r\n");
            ret = at_man->send_cmd(air724, "AT", 500, 3000);
            sysprt->alog("[AT test] AT result = %d\r\n", ret);
            at_man->dump_rx(air724);

            vTaskDelay(pdMS_TO_TICKS(2000));

            /* 测试 2: 查询 SIM 卡状态 */
            sysprt->alog("[AT test] sending AT+CPIN?...\r\n");
            ret = at_man->send_cmd(air724, "AT+CPIN?", 500, 3000);
            sysprt->alog("[AT test] AT+CPIN? result = %d\r\n", ret);
            at_man->dump_rx(air724);

            vTaskDelay(pdMS_TO_TICKS(2000));

            /* 测试 3: 查询信号质量 */
            sysprt->alog("[AT test] sending AT+CSQ...\r\n");
            ret = at_man->send_cmd(air724, "AT+CSQ", 500, 3000);
            sysprt->alog("[AT test] AT+CSQ result = %d\r\n", ret);
            at_man->dump_rx(air724);
        }

        /* 每 10 秒循环一次测试 */
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_start);
