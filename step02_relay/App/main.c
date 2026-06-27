/**
 * Step 2: 寄存器级 GPIO 控制两个继电器
 *
 * 目标: 直接操作寄存器控制 ELC0(M2) / ELC1(B12) 两个继电器，
 *       理解 PORT mux 机制和 GPIO 寄存器映射。
 *
 * 芯片: SWM320RET7 (ARM Cortex-M4, 110.592MHz)
 * 引脚: ELC0 = M2  (GPIOM PIN2)  — 继电器0
 *       ELC1 = B12 (GPIOB PIN12) — 继电器1
 *
 * 寄存器映射:
 *   GPIOB 基址: 0x40012000
 *   GPIOM 基址: 0x40015000
 *   PORT 基址:  0x40010000
 *   SYSCON 基址: 0x40000000
 *
 * PORT MUX 选择:
 *   每个引脚 2bit: 00=GPIO, 01=功能0, 10=功能1, 11=功能2
 *   B12 在 SEL 的 bit[25:24] → 清0 = GPIO
 *   M2  在 SEL0 的 bit[5:4]   → 清0 = GPIO
 */

#include "SWM320.h"

/*---------------------------------------------------------------------------
 * GPIO 寄存器定义 (直接地址映射)
 *---------------------------------------------------------------------------*/
#define Addr_GPIOB_Base     0x40012000
#define Addr_GPIOM_Base     0x40015000
#define Addr_PORT_Base      0x40010000
#define Addr_SYSCON_Base    0x40000000

#define Offset_GPIO_DATA    0x00
#define Offset_GPIO_DIR     0x04

/* PORT 引脚选择寄存器 */
#define Offset_PORTB_SEL    0x04
#define Offset_PORTM_SEL0   0x20

/* PORT 输入使能寄存器 */
#define Offset_PORTB_INEN   0x610
#define Offset_PORTM_INEN   0x640

/* 系统时钟使能寄存器 */
#define Offset_SYS_CLKEN    0x08

/*---- GPIOB 寄存器 ----*/
#define rGPIOB_DATA   (*(volatile unsigned int*)(Addr_GPIOB_Base + Offset_GPIO_DATA))
#define rGPIOB_DIR    (*(volatile unsigned int*)(Addr_GPIOB_Base + Offset_GPIO_DIR))

/*---- GPIOM 寄存器 ----*/
#define rGPIOM_DATA   (*(volatile unsigned int*)(Addr_GPIOM_Base + Offset_GPIO_DATA))
#define rGPIOM_DIR    (*(volatile unsigned int*)(Addr_GPIOM_Base + Offset_GPIO_DIR))

/*---- PORT 选择寄存器 ----*/
#define rPORTB_SEL    (*(volatile unsigned int*)(Addr_PORT_Base + Offset_PORTB_SEL))
#define rPORTM_SEL0   (*(volatile unsigned int*)(Addr_PORT_Base + Offset_PORTM_SEL0))

/*---- PORT 输入使能 ----*/
#define rPORTB_INEN   (*(volatile unsigned int*)(Addr_PORT_Base + Offset_PORTB_INEN))
#define rPORTM_INEN   (*(volatile unsigned int*)(Addr_PORT_Base + Offset_PORTM_INEN))

/*---- 时钟使能 ----*/
#define rSYS_CLKEN    (*(volatile unsigned int*)(Addr_SYSCON_Base + Offset_SYS_CLKEN))

/* 时钟使能位: bit1=GPIOB, bit4=GPIOM */
#define CLKEN_GPIOB   (1 << 1)
#define CLKEN_GPIOM   (1 << 4)

/* 继电器引脚位 */
#define RELAY_ELC0    (1 << 2)   /* M2  — GPIOM PIN2  */
#define RELAY_ELC1    (1 << 12)  /* B12 — GPIOB PIN12 */

/*---------------------------------------------------------------------------
 * 继电器名称
 *---------------------------------------------------------------------------*/
#define RELAY0_NAME   "ELC0(M2)"
#define RELAY1_NAME   "ELC1(B12)"

/*---------------------------------------------------------------------------
 * 简易阻塞延时 (~0.5s)
 *---------------------------------------------------------------------------*/
void delay_approx(void)
{
    volatile uint32_t d = 2000000;
    while (d--);
}

/*---------------------------------------------------------------------------
 * 长延时 (~1.5s) — 用于继电器状态保持观察
 *---------------------------------------------------------------------------*/
void delay_long(void)
{
    volatile uint32_t d = 6000000;
    while (d--);
}

/*---------------------------------------------------------------------------
 * 初始化两个继电器引脚为 GPIO 输出
 *
 * 步骤:
 *   1. 使能 GPIOB / GPIOM 时钟
 *   2. PORT_SEL 清0 → 引脚功能选 GPIO
 *   3. GPIO_DIR 置1  → 方向为输出
 *   4. PORT_INEN 清0 → 关闭输入（输出模式不需要）
 *   5. GPIO_DATA 设初始电平
 *
 * 参数: init_high — 1=初始高电平(继电器吸合), 0=初始低电平(断开)
 *---------------------------------------------------------------------------*/
void relay_init(int init_high)
{
    /* 1. 使能 GPIOB 和 GPIOM 时钟 */
    rSYS_CLKEN |= (CLKEN_GPIOB | CLKEN_GPIOM);

    /* 2. PORT_SEL 清0 → B12 和 M2 设为 GPIO 功能
     *    B12 占用 SEL 的 bit[25:24]，M2 占用 SEL0 的 bit[5:4]
     *    每个引脚 2bit 选功能：00=GPIO */
    rPORTB_SEL  &= ~(3 << 24);   /* B12: 清 bit[25:24] */
    rPORTM_SEL0 &= ~(3 << 4);    /* M2:  清 bit[5:4]   */

    /* 3. GPIO_DIR 置1 → 设为输出 */
    rGPIOB_DIR |= RELAY_ELC1;    /* B12 输出 */
    rGPIOM_DIR |= RELAY_ELC0;    /* M2  输出 */

    /* 4. 关闭输入使能（输出模式下不需要） */
    rPORTB_INEN &= ~RELAY_ELC1;
    rPORTM_INEN &= ~RELAY_ELC0;

    /* 5. 设置初始电平 */
    if (init_high)
    {
        rGPIOB_DATA |= RELAY_ELC1;
        rGPIOM_DATA |= RELAY_ELC0;
    }
    else
    {
        rGPIOB_DATA &= ~RELAY_ELC1;
        rGPIOM_DATA &= ~RELAY_ELC0;
    }
}

/*---------------------------------------------------------------------------
 * 控制单个继电器
 *
 * 参数: relay — RELAY_ELC0 或 RELAY_ELC1
 *       on     — 1=吸合(高电平), 0=断开(低电平)
 *---------------------------------------------------------------------------*/
void relay_ctrl(unsigned int relay, int on)
{
    if (relay == RELAY_ELC0)
    {
        if (on)
            rGPIOM_DATA |= RELAY_ELC0;
        else
            rGPIOM_DATA &= ~RELAY_ELC0;
    }
    else if (relay == RELAY_ELC1)
    {
        if (on)
            rGPIOB_DATA |= RELAY_ELC1;
        else
            rGPIOB_DATA &= ~RELAY_ELC1;
    }
}

/*---------------------------------------------------------------------------
 * 读取单个继电器当前状态
 *
 * 返回: 1=高电平(吸合), 0=低电平(断开)
 *---------------------------------------------------------------------------*/
int relay_read(unsigned int relay)
{
    if (relay == RELAY_ELC0)
        return (rGPIOM_DATA & RELAY_ELC0) ? 1 : 0;
    else if (relay == RELAY_ELC1)
        return (rGPIOB_DATA & RELAY_ELC1) ? 1 : 0;
    return 0;
}

/*---------------------------------------------------------------------------
 * UART0 初始化 — printf 调试输出
 * PA2=RX, PA3=TX, 115200-8-N-1
 *---------------------------------------------------------------------------*/
void SerialInit(void)
{
    UART_InitStructure u;

    PORT_Init(PORTA, PIN2, FUNMUX0_UART0_RXD, 1);
    PORT_Init(PORTA, PIN3, FUNMUX1_UART0_TXD, 0);

    u.Baudrate       = 115200;
    u.DataBits       = UART_DATA_8BIT;
    u.Parity         = UART_PARITY_NONE;
    u.StopBits       = UART_STOP_1BIT;
    u.RXThreshold    = 3;
    u.RXThresholdIEn = 0;
    u.TXThreshold    = 3;
    u.TXThresholdIEn = 0;
    u.TimeoutTime    = 10;
    u.TimeoutIEn     = 0;
    UART_Init(UART0, &u);
    UART_Open(UART0);
}

/*---------------------------------------------------------------------------
 * printf 重定向到 UART0 (需勾选 MDK "Use MicroLIB")
 *---------------------------------------------------------------------------*/
int fputc(int ch, FILE *f)
{
    UART_WriteByte(UART0, ch);
    while (UART_IsTXBusy(UART0));
    return ch;
}

/*---------------------------------------------------------------------------
 * 打印单个继电器状态
 *---------------------------------------------------------------------------*/
void print_relay_state(const char *name, unsigned int relay)
{
    printf("  %-12s : %s (%s)\r\n",
           name,
           relay_read(relay) ? "HIGH" : "LOW ",
           relay_read(relay) ? "吸合" : "断开");
}

/*---------------------------------------------------------------------------
 * 打印所有继电器状态
 *---------------------------------------------------------------------------*/
void print_all_states(void)
{
    printf("--- 继电器状态 ---\r\n");
    print_relay_state(RELAY0_NAME, RELAY_ELC0);
    print_relay_state(RELAY1_NAME, RELAY_ELC1);
    printf("------------------\r\n\r\n");
}

/*---------------------------------------------------------------------------
 * 主函数
 *
 * 测试流程: 依次控制两个继电器吸合/断开，串口输出状态变化
 *---------------------------------------------------------------------------*/
int main(void)
{
    SystemInit();
    SerialInit();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Step 2: 寄存器级 GPIO — 继电器控制\r\n");
    printf("  CPU Clock: %d Hz\r\n", SystemCoreClock);
    printf("========================================\r\n\r\n");

    printf("[INFO] 寄存器映射:\r\n");
    printf("  GPIOB_BASE = 0x%08X\r\n", Addr_GPIOB_Base);
    printf("  GPIOM_BASE = 0x%08X\r\n", Addr_GPIOM_Base);
    printf("  PORT_BASE  = 0x%08X\r\n", Addr_PORT_Base);
    printf("  SYSCON_CLKEN = 0x%08X\r\n", rSYS_CLKEN);
    printf("\r\n");

    /* 初始化: 继电器默认断开 (低电平) */
    printf("[INIT] 初始化继电器...\r\n");
    relay_init(0);
    print_all_states();

    while (1)
    {
        /* 测试序列 1: ELC0 吸合 → 断开 */
        printf(">>> 测试 1/4: %s 吸合\r\n", RELAY0_NAME);
        relay_ctrl(RELAY_ELC0, 1);
        print_all_states();
        delay_long();

        printf(">>> 测试 2/4: %s 断开\r\n", RELAY0_NAME);
        relay_ctrl(RELAY_ELC0, 0);
        print_all_states();
        delay_long();

        /* 测试序列 2: ELC1 吸合 → 断开 */
        printf(">>> 测试 3/4: %s 吸合\r\n", RELAY1_NAME);
        relay_ctrl(RELAY_ELC1, 1);
        print_all_states();
        delay_long();

        printf(">>> 测试 4/4: %s 断开\r\n", RELAY1_NAME);
        relay_ctrl(RELAY_ELC1, 0);
        print_all_states();
        delay_long();

        printf("===== 一轮测试完成，3s 后重复 =====\r\n\r\n");
        delay_long();
        delay_long();
    }
}
