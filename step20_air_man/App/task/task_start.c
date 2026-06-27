/**
 * task_start.c — 启动任务
 *
 * 职责:
 *   - 上电后第一个运行
 *   - 初始化硬件 (IO / UART0 / UART1)
 *   - 初始化 4G 模块管理层 (air_man ← at_dev ← uart1)
 *   - 打印版本信息
 *   - 依次创建 host → listen → ctrl 任务
 *   - 主循环: 4G 模块测试 (后续 Step 21 填充网络调度)
 *
 * 初始化顺序 (严格):
 *   main() → task_start->create() → task_entry()
 *     → uart0->init()        ← 必须最先! printf 依赖它
 *     → sysprt->init()       ← 创建消息队列 (日志系统就绪)
 *     → io_init_all()        ← 配置 GPIO 模式 (printf 可用)
 *     → init_io_pin()        ← 设置输出初始电平
 *     → uart1->init()        ← 4G 模块 UART (115200)
 *     → air_man->create_dev()← 创建 4G 设备 (内建 at_dev)
 *     → show_banner()
 *     → task_host->create()
 *     → task_listen->create()
 *     → task_ctrl->create()
 *     → while(1) { 4G test + vTaskDelay; }   // air_man 测试
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
#include "air_man.h"

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
    printf("  Step 20: 4G Module Manager (air_man)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  UART0     : 115200 bps (debug console)\r\n");
    printf("  UART1     : 115200 bps (AIR724 4G module)\r\n");
    printf("  Layers    : air_man → at_dev → uart1\r\n");
    printf("========================================\r\n");
    printf("\r\n");
}

/* ================================================================
 * task_entry — 启动任务入口
 *
 * 初始化所有硬件 + 软件模块, 然后进入 AT 便捷命令测试循环。
 *
 * air_man 封装了 at_dev:
 *   at_dev:    通用 AT 引擎 — send_cmd("AT+CSQ") → OK/ERROR
 *   air_man:   设备管理层 — CSQ(&rssi, &ber) → 解析后的信号值
 * ================================================================ */
static void task_entry(void *arg)
{
    (void)arg;

    /*
     * 初始化顺序严格:
     *   UART → GPIO → 电平设定 → 打印 → 创建任务
     */

    /* ---- 1. UART0 初始化 (必须最先, printf 依赖) ---- */
    uart0->init(115200);

    /* ---- 2. 日志系统初始化 ---- */
    sysprt->init();

    /* ---- 3. GPIO 模式配置 ---- */
    io_init_all();

    /* ---- 4. 输出引脚初始电平 ---- */
    drv_gpio->write(PIN_ELC0, PIN_LOW);
    drv_gpio->write(PIN_ELC1, PIN_LOW);
    drv_gpio->write(PIN_SLED, PIN_LOW);
    drv_gpio->write(PIN_NETRST, PIN_HIGH);

    /* ---- 5. 灯板初始化 ---- */
    led_board->init();

    /* ---- 6. HLW8012 计量初始化 ---- */
    drv_hlw8012->init();

    /* ---- 7. 保险丝检测初始化 ---- */
    drv_fuse->init();

    /* ---- 8. FlashDB 配置加载 ---- */
    flashdb->init();

    /* ---- 9. UART1 初始化 (4G 模块) ---- */
    uart1->init(115200);

    /* ---- 10. 创建 4G 模块设备 (air_man 内建 at_dev) ---- */
    air724 = air_man->create_dev(uart1);
    if (air724) {
        sysprt->alog("[task_start] AIR724 device created\r\n");
    }

    /* ---- 11. LED 演示动画 ---- */
    led_board->set_sock(SOCK_0, SOCK_MODE_CHARGING);
    led_board->set_sock(SOCK_1, SOCK_MODE_CHARGING);
    led_board->set_net(1);
    sysprt->alog("[led_board] charging animation started\r\n");

    /* ---- 12. 打印版本信息 ---- */
    show_banner();

    /* ---- 13. 创建 host 任务 ---- */
    task_host->create(NULL, NULL);

    /* ---- 14. 创建 listen 任务 ---- */
    task_listen->create(NULL, NULL);

    /* ---- 15. 创建 ctrl 任务 ---- */
    task_ctrl->create(NULL, NULL);

    /* ---- 16. 主循环: air_man 便捷命令测试 ---- */
    while (1) {
        if (air724) {
            int ret;
            int rssi, ber;
            char sim_stat[16];

            /* 测试 1: AT → 模块在线? */
            sysprt->alog("[air_man] AT...\r\n");
            ret = air724->AT(air724);
            sysprt->alog("[air_man] AT = %d\r\n", ret);
            air_man->ptf_rxbuf(air724);

            vTaskDelay(pdMS_TO_TICKS(2000));

            /* 测试 2: CPIN → SIM 卡状态 (自动解析) */
            sysprt->alog("[air_man] CPIN...\r\n");
            sim_stat[0] = '\0';
            ret = air724->CPIN(air724, sim_stat, sizeof(sim_stat));
            sysprt->alog("[air_man] CPIN = %d, SIM: %s\r\n", ret, sim_stat);
            air_man->ptf_rxbuf(air724);

            vTaskDelay(pdMS_TO_TICKS(2000));

            /* 测试 3: CSQ → 信号质量 (自动解析 rssi/ber) */
            sysprt->alog("[air_man] CSQ...\r\n");
            rssi = 0; ber = 0;
            ret = air724->CSQ(air724, &rssi, &ber);
            sysprt->alog("[air_man] CSQ = %d, rssi=%d, ber=%d\r\n",
                         ret, rssi, ber);
            air_man->ptf_rxbuf(air724);
        }

        /* 每 10 秒循环一次测试 */
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_start);
