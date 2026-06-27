/**
 * Step 25: BootLoader (bare-metal)
 *
 * 最简 BootLoader — 无 FreeRTOS, 无驱动层, 直接操作 SDK。
 *
 * Flash 分区 (512KB):
 *   0x00000 - 0x0FFFF  ( 64KB)  boot   — 当前 BootLoader
 *   0x10000 - 0x10FFF  (  4KB)  conf   — 启动标志
 *   0x11800 - 0x3E7FF  (186KB)  app    — 应用程序
 *   0x7C000 - 0x7CFFF  (  4KB)  fdb    — FlashDB 配置存储
 *
 * 流程:
 *   SystemInit → SerialInit → 读 0x10000 处标志
 *     → 0x5A5A5A5A → jump_to_app(0x11800)
 *     → 其他值     → LED 闪烁, 等待 APP 烧录
 */

#include "SWM320.h"
#include "flash_partition.h"
#include <stdio.h>

/* ---- 前向声明 ---- */
static void SerialInit(void);
static void jump_to_app(uint32_t addr);
static void delay_ms(volatile uint32_t ms);

/* ================================================================
 * main — BootLoader 入口
 * ================================================================ */
int main(void)
{
    uint32_t boot_flag;

    SystemInit();
    SerialInit();

    printf("----------------------------------\r\n");
    printf("BootLoader v1.0 (bare-metal)\r\n");
    printf("Build: %s %s\r\n", __DATE__, __TIME__);
    printf("CPU: SWM320 @ 110.592MHz\r\n");

    /* 读启动标志 (CONF 扇区, 直接内存映射读取) */
    boot_flag = *(volatile uint32_t *)PART_CONF_START;
    printf("Boot flag @0x%08X: 0x%08X\r\n", PART_CONF_START, boot_flag);

    if (boot_flag == PART_CONF_MAGIC) {
        printf("APP valid -> jumping to 0x%08X...\r\n\r\n", PART_APP_START);

        /* 跳转前清理外设 (与 024_BL 参考代码一致) */
        SysTick->CTRL  = 0;
        SysTick->VAL   = 0;
        UART_Close(UART0);
        NVIC_DisableIRQ(UART0_IRQn);

        jump_to_app(PART_APP_START);

        /* 不应到达 */
        while (1) __NOP();
    }

    /* 没有有效 APP — 等待烧录 */
    printf("No valid APP (flag=0x%08X).\r\n", boot_flag);
    printf("Staying in BL, LED blinking at ~1Hz on PC4.\r\n");
    printf("Flash APP at 0x11800 to proceed.\r\n");

    /* 初始化 LED 引脚 */
    PORT_Init(PORTC, LED_PIN, PORTC_PIN4_GPIO, 0);
    GPIO_INIT(LED_PORT, LED_PIN, GPIO_OUTPUT);

    while (1) {
        GPIO_SetBit(LED_PORT, LED_PIN);
        delay_ms(500);
        GPIO_ClrBit(LED_PORT, LED_PIN);
        delay_ms(500);
    }
}

/* ================================================================
 * SerialInit — UART0 初始化 (115200-8-N-1)
 *
 * 与 024_BL/APP/main.c 的 SerialInit() 完全一致。
 * PA2=UART0_RXD (FUNMUX0, 上拉), PA3=UART0_TXD (FUNMUX1, 推挽)
 * ================================================================ */
static void SerialInit(void)
{
    UART_InitStructure UART_initStruct;

    /* 引脚复用 */
    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);  /* RX: 上拉输入 */
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);  /* TX: 推挽输出 */

    /* UART 参数 */
    UART_initStruct.Baudrate       = 115200;
    UART_initStruct.DataBits       = UART_DATA_8BIT;
    UART_initStruct.Parity         = UART_PARITY_NONE;
    UART_initStruct.StopBits       = UART_STOP_1BIT;
    UART_initStruct.RXThresholdIEn = 0;
    UART_initStruct.TXThresholdIEn = 0;
    UART_initStruct.TimeoutIEn     = 0;

    UART_Init(UART0, &UART_initStruct);
    UART_Open(UART0);
}

/* ================================================================
 * fputc — printf 重定向到 UART0 (阻塞式)
 * ================================================================ */
int fputc(int ch, FILE *f)
{
    (void)f;
    while (UART_IsTXFIFOFull(UART0));
    UART_WriteByte(UART0, (uint8_t)ch);
    return ch;
}

/* ================================================================
 * jump_to_app — Cortex-M 标准 APP 跳转
 *
 * 步骤:
 *   1. 关全局中断
 *   2. 从 APP 起始地址读 MSP (前 4 字节)
 *   3. 从 APP 起始地址+4 读 PC/Reset_Handler (后 4 字节)
 *   4. 设置 SCB->VTOR = APP 基址 (中断向量表重定位)
 *   5. 设置 MSP = APP 的栈指针
 *   6. 跳转到 APP 的 Reset_Handler
 *
 * 与 024_BL/task_start.c 和 V5.5 BL 的实现完全一致。
 * ================================================================ */
static void jump_to_app(uint32_t addr)
{
    uint32_t sp;
    uint32_t pc;

    __disable_irq();

    /* APP 向量表: offset 0 = 初始 MSP, offset 4 = Reset_Handler */
    sp = *(volatile uint32_t *)(addr);
    pc = *(volatile uint32_t *)(addr + 4);

    /* 重定位向量表 — 让中断路由到 APP 的 ISR */
    SCB->VTOR = addr;

    /* 切换到 APP 的栈 */
    __set_MSP(sp);

    /* 跳转到 APP 的复位处理程序
     * 这会执行 APP 的 SystemInit() → 运行时初始化 → main() */
    ((void (*)(void))pc)();

    /* 如果 APP 的 Reset_Handler 返回 (不应发生), 死循环 */
    while (1) __NOP();
}

/* ================================================================
 * delay_ms — 简易 NOP 延时 (无 SysTick 依赖)
 *
 * 110.592MHz 下, 每个 NOP ≈ 9ns.
 * 1ms ≈ 110592 次 NOP. 实际值经粗略校准。
 * ================================================================ */
static void delay_ms(volatile uint32_t ms)
{
    while (ms--) {
        volatile uint32_t i;
        for (i = 0; i < 110000; i++) {
            __NOP();
        }
    }
}
