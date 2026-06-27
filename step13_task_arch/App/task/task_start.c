/**
 * task_start.c — 启动任务
 *
 * 职责:
 *   - 上电后第一个运行
 *   - 初始化硬件 (IO / UART0)
 *   - 打印版本信息
 *   - 依次创建 host → listen → ctrl 任务
 *   - 主循环: 占位 (后续 Step 21 填充网络调度)
 *
 * 初始化顺序 (严格):
 *   main() → task_start->create() → task_entry()
 *     → uart0->init()        ← 必须最先! printf 依赖它
 *     → io_init_all()        ← 配置 GPIO 模式 (printf 可用)
 *     → init_io_pin()        ← 设置输出初始电平
 *     → show_banner()
 *     → task_host->create()
 *     → task_listen->create()
 *     → task_ctrl->create()
 *     → while(1) { vTaskDelay(5); }   // 占位
 */

#include "task_inc.h"
#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drv_uart0.h"
#include "drv_gpio.h"
#include "io_config.h"

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
    printf("  Step 13: Multi-Task Architecture\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  Pattern   : task_body_t (4 tasks)\r\n");
    printf("  Tasks     : start / host / ctrl / listen\r\n");
    printf("========================================\r\n");
    printf("\r\n");
}

/* ================================================================
 * task_entry — 启动任务入口
 *
 * 执行顺序:
 *   1. IO 初始化 (安全状态)
 *   2. UART0 初始化 (115200)
 *   3. 打印版本信息
 *   4. 创建 host 任务 (LED + 命令行)
 *   5. 创建 listen 任务 (占位 — 后续 HLW8012)
 *   6. 创建 ctrl 任务 (占位 — 后续充电控制)
 *   7. 进入主循环 (占位 — 后续网络调度)
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

    /* ---- 2. GPIO 模式配置 (PORT_Init + GPIO_INIT, printf 可用) ---- */
    io_init_all();

    /* ---- 3. 输出引脚初始电平 (继电器断开 / LED 熄灭 / NetRst 高) ---- */
    drv_gpio->write(PIN_ELC0, PIN_LOW);
    drv_gpio->write(PIN_ELC1, PIN_LOW);
    drv_gpio->write(PIN_SLED, PIN_LOW);
    drv_gpio->write(PIN_NETRST, PIN_HIGH);

    /* ---- 4. 打印版本信息 ---- */
    show_banner();

    /* ---- 5. 创建 host 任务 (LED 刷新 + 命令行) ---- */
    task_host->create(NULL, NULL);

    /* ---- 6. 创建 listen 任务 (占位 — 后续 HLW8012 计量) ---- */
    task_listen->create(NULL, NULL);

    /* ---- 7. 创建 ctrl 任务 (占位 — 后续充电控制 + OTA) ---- */
    task_ctrl->create(NULL, NULL);

    /* ---- 8. 主循环 (占位 — 后续 Step 21 网络调度) ---- */
    while (1) {
        vTaskDelay(5);
    }
}

__task_body_end;

/* ---- 导出全局指针 ---- */
__task_body_quote_to(task_start);
