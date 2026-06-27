/**
 * Step 26: BootLoader v2.0 (FreeRTOS + OTA)
 *
 * 启动流程:
 *   SystemInit → SerialInit(UART0) → 读 boot_app @ 0x10004
 *     → _APP_FAC (或未初始化) → jump_to_app(0x11800)
 *     → _APP_UPDATE → 初始化 FreeRTOS → OTA 状态机下载固件
 *
 * Flash 分区:
 *   0x00000 - 0x0FFFF  ( 64KB)  boot   — 本 BL (FreeRTOS + 4G + OTA)
 *   0x10000 - 0x10FFF  (  4KB)  conf   — boot_config_t
 *   0x11800 - 0x3E7FF  (186KB)  app    — 应用程序
 *   0x7C000 - 0x7CFFF  (  4KB)  fdb    — FlashDB (校准+凭据)
 */

#include "SWM320.h"
#include "FreeRTOS.h"
#include "task.h"
#include "flash_partition.h"
#include <stdio.h>

/* ---- 前向声明 ---- */
static void SerialInit(void);
static void jump_to_app(uint32_t addr);
extern void create_task_start(void);

/* ---- printf 重定向 ---- */
int fputc(int ch, FILE *f)
{
    (void)f;
    while (UART_IsTXFIFOFull(UART0));
    UART_WriteByte(UART0, (uint8_t)ch);
    return ch;
}

/* ================================================================
 * main — BootLoader 入口
 * ================================================================ */
int main(void)
{
    uint32_t boot_app;

    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("==================================\r\n");
    printf("BootLoader v2.0 (FreeRTOS + OTA)\r\n");
    printf("Build: %s %s\r\n", __DATE__, __TIME__);
    printf("==================================\r\n");

    /* 读 boot_app (内存映射, 无需初始化任何模块) */
    boot_app = BOOT_CFG_PTR->boot_app;
    printf("boot_app @0x%08X = %lu\r\n",
           (uint32_t)(&BOOT_CFG_PTR->boot_app), boot_app);

    if (BOOT_CFG_PTR->boot_code == BOOT_CODE_MAGIC &&
        boot_app == _APP_UPDATE) {
        /* OTA 模式 — 留在 BL, 初始化 FreeRTOS */
        printf("OTA mode requested, initializing FreeRTOS...\r\n\r\n");

        /* 创建启动任务 */
        create_task_start();

        /* 启动 FreeRTOS 调度器 */
        vTaskStartScheduler();

        /* 不应到达 */
        while (1);
    }

    /* 正常模式 — 跳转到 APP */
    if (BOOT_CFG_PTR->boot_code == BOOT_CODE_MAGIC) {
        printf("boot_app = _APP_FAC, jumping to APP...\r\n\r\n");
    } else {
        printf("CONF not initialized, jumping to APP "
               "(APP will init on first boot)...\r\n\r\n");
    }

    /* 跳转前清理外设 */
    SysTick->CTRL  = 0;
    SysTick->VAL   = 0;
    UART_Close(UART0);
    NVIC_DisableIRQ(UART0_IRQn);

    jump_to_app(PART_APP_START);

    /* 不应到达 */
    while (1) __NOP();
}

/* ================================================================
 * SerialInit — UART0 初始化 (115200-8-N-1)
 * ================================================================ */
static void SerialInit(void)
{
    UART_InitStructure uis;

    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);  /* RX: 上拉 */
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);  /* TX: 推挽 */

    uis.Baudrate       = 115200;
    uis.DataBits       = UART_DATA_8BIT;
    uis.Parity         = UART_PARITY_NONE;
    uis.StopBits       = UART_STOP_1BIT;
    uis.RXThresholdIEn = 0;
    uis.TXThresholdIEn = 0;
    uis.TimeoutIEn     = 0;

    UART_Init(UART0, &uis);
    UART_Open(UART0);
}

/* ================================================================
 * jump_to_app — 标准 Cortex-M APP 跳转
 * ================================================================ */
static void jump_to_app(uint32_t addr)
{
    uint32_t sp;
    uint32_t pc;

    __disable_irq();

    sp = *(volatile uint32_t *)(addr);
    pc = *(volatile uint32_t *)(addr + 4);

    SCB->VTOR = addr;
    __set_MSP(sp);
    ((void (*)(void))pc)();

    while (1) __NOP();
}
