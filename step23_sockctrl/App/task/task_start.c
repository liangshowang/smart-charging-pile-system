/**
 * task_start.c — 启动任务
 *
 * 职责:
 *   - 上电后第一个运行
 *   - 初始化硬件 (IO / UART0 / UART1)
 *   - 初始化 4G 模块管理层 (air_man ← at_dev ← uart1)
 *   - 启动网络状态机 (net_man: 复位→初始化→连接→就绪)
 *   - 打印版本信息
 *   - 依次创建 host → listen → ctrl 任务
 *   - 主循环: 驱动 net_man 状态机, 网络就绪后进入空闲
 *
 * 初始化顺序 (严格):
 *   main() → task_start->create() → task_entry()
 *     → uart0->init()        ← 必须最先! printf 依赖它
 *     → sysprt->init()       ← 创建消息队列 (日志系统就绪)
 *     → io_init_all()        ← 配置 GPIO 模式 (printf 可用)
 *     → 输出引脚初始电平     ← NetRst 拉高 (正常模式)
 *     → uart1->init()        ← 4G 模块 UART (115200)
 *     → air_man->create_dev()← 创建 4G 设备 (内建 at_dev)
 *     → net_man->init()      ← 初始化状态机
 *     → show_banner()
 *     → task_host->create()
 *     → task_listen->create()
 *     → task_ctrl->create()
 *     → while(1) { net_man->work(); }  ← 驱动入网流程
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
#include "net_man.h"
#include "message.h"

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
    printf("  Step 23: Socket Control (sock_ctrl)\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  UART0     : 115200 bps (debug console)\r\n");
    printf("  UART1     : 115200 bps (AIR724 4G module)\r\n");
    printf("  Stack     : sock_ctrl→msg→net_man→air_man→at_dev→uart1\r\n");
    printf("  Server    : www.armsoc.cn:9002\r\n");
    printf("  Features  : charging FSM + safety check + event report\r\n");
    printf("========================================\r\n");
    printf("\r\n");
}

/* ================================================================
 * task_entry — 启动任务入口
 *
 * 初始化所有硬件 + 软件模块, 然后驱动 net_man 状态机完成入网。
 *
 * 入网流程 (net_man 自动完成):
 *   Phase 0: 硬件复位 → 等 45s → AT 握手
 *   Phase 1: CGMI→CGMR→CPIN→CSQ→CREG→CGATT
 *           →CIPMODE→CIPMUX→CSTT→CIICR→CIFSR
 *   Phase 2: CIPSTART → LOGIN → wait Logon
 *   Phase 3: READY (网络就绪, 等待后续协议层使用)
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
    drv_gpio->write(PIN_NETRST, PIN_HIGH);   /* 4G 模块正常运行 */

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

    /* ---- 11. 初始化网络状态机 ---- */
    net_man->init();

    /* ---- 12. LED 演示动画 ---- */
    led_board->set_sock(SOCK_0, SOCK_MODE_CHARGING);
    led_board->set_sock(SOCK_1, SOCK_MODE_CHARGING);
    led_board->set_net(0);   /* 网络灯灭, 等连上再亮 */
    sysprt->alog("[led_board] charging animation started\r\n");

    /* ---- 13. 打印版本信息 ---- */
    show_banner();

    /* ---- 14. 创建 host 任务 ---- */
    task_host->create(NULL, NULL);

    /* ---- 15. 创建 listen 任务 ---- */
    task_listen->create(NULL, NULL);

    /* ---- 16. 创建 ctrl 任务 ---- */
    task_ctrl->create(NULL, NULL);

    /* ---- 17. 主循环: 驱动网络状态机 ---- */
    while (1) {
        if (air724 == NULL) {
            /* 设备未创建, 等待重试 */
            sysprt->aerr("[task_start] air724 is NULL, retry create...\r\n");
            air724 = air_man->create_dev(uart1);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* 驱动状态机前进一步 (阻塞执行一条 AT 命令) */
        net_man->work();

        /* 检查是否已就绪 */
        if (net_man->is_ready()) {
            /*
             * 网络就绪 — 协议层 (message) 已在 net_man 内部接管:
             *   - 收包解析 (命令行派发)
             *   - 心跳保活 (PING 每 10s)
             *   - 事件池 (可靠重试上报)
             *   - 超时重连 (30s 无数据 → 复位)
             */
            static int ready_printed = 0;

            if (!ready_printed) {
                int phase, step, reconnect;
                net_man->get_state(&phase, &step, &reconnect);

                /* 点亮网络灯 */
                led_board->set_net(1);

                sysprt->alog("[task_start] ===========================\r\n");
                sysprt->alog("[task_start] Network READY!\r\n");
                sysprt->alog("[task_start] Protocol engine active\r\n");
                sysprt->alog("[task_start] PING interval: 10s\r\n");
                sysprt->alog("[task_start] Recv timeout : 30s\r\n");
                sysprt->alog("[task_start] Phase=%d Reconn=%d\r\n",
                             phase, reconnect);
                sysprt->alog("[task_start] ===========================\r\n");

                ready_printed = 1;
            }

            /* 让出 CPU (协议引擎已有自己的 50ms 延迟) */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_start);
