/**
 * drv_hc595.c — HC595 移位寄存器驱动实现
 *
 * 通过 drv_gpio->write() 模拟串行时序 (bit-bang):
 *   DAT: 串行数据线
 *   CLK: 移位时钟, 上升沿有效
 *   UD:  锁存时钟, 上升沿将移位寄存器 → 输出寄存器
 *
 * 两片级联: 先发后级字节 (disp[1]), 再发前级字节 (disp[0])。
 */

#include "drv_hc595.h"
#include "drv_gpio.h"

/* ---- 延时参数 (循环次数, 取决于 CPU 频率) ---- */
#define WIDE_CLK    20    /* CLK 半周期 */
#define WIDE_UD     50    /* UD 脉冲宽度 */

/* ================================================================
 * delay_loop — 简易忙等延时
 * ================================================================ */
static void delay_loop(int n)
{
    volatile int i;
    for (i = 0; i < n; i++);
}

/* ================================================================
 * write_byte — 发送单字节 (MSB 先)
 *
 * 时钟线空闲为高电平。
 * 每个 bit: DAT=bit → CLK↓ → CLK↑ (上升沿移位)
 * ================================================================ */
static void write_byte(hc595_pin_pt bus, uint8_t dat)
{
    int i;

    for (i = 0; i < 8; i++) {
        /* 设数据 (MSB first) */
        drv_gpio->write(bus->dat, (dat & 0x80) ? PIN_HIGH : PIN_LOW);
        dat <<= 1;

        /* CLK 脉冲: 低→高 (上升沿移位) */
        drv_gpio->write(bus->clk, PIN_LOW);
        delay_loop(WIDE_CLK);
        drv_gpio->write(bus->clk, PIN_HIGH);
        delay_loop(WIDE_CLK);
    }
}

/* ================================================================
 * do_init — 初始化 HC595 引脚
 *
 * 全部设为输出, 空闲状态:
 *   DAT = LOW
 *   CLK = HIGH
 *   UD  = HIGH
 * ================================================================ */
static void do_init(hc595_pin_pt bus)
{
    drv_gpio->set_mode(bus->dat, PIN_MODE_OUTPUT);
    drv_gpio->set_mode(bus->clk, PIN_MODE_OUTPUT);
    drv_gpio->set_mode(bus->ud,  PIN_MODE_OUTPUT);

    /* 空闲电平 */
    drv_gpio->write(bus->dat, PIN_LOW);
    drv_gpio->write(bus->clk, PIN_HIGH);
    drv_gpio->write(bus->ud,  PIN_HIGH);
}

/* ================================================================
 * do_write — 写入移位寄存器
 *
 * 逆序发送: buf[len-1] 先发 (最终在后级芯片),
 *           buf[0] 最后发 (最终在前级芯片)。
 * ================================================================ */
static void do_write(hc595_pin_pt bus, uint8_t *buf, uint8_t len)
{
    int i;

    if (len == 0) return;

    /* 逆序: 最后一个字节先发 */
    for (i = len - 1; i >= 0; i--)
        write_byte(bus, buf[i]);
}

/* ================================================================
 * do_update — 锁存输出
 *
 * UD: 高→低→高, 上升沿将移位寄存器的值锁存到输出寄存器。
 * ================================================================ */
static void do_update(hc595_pin_pt bus)
{
    drv_gpio->write(bus->ud, PIN_LOW);
    delay_loop(WIDE_UD);
    drv_gpio->write(bus->ud, PIN_HIGH);
    delay_loop(WIDE_UD);
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static hc595_opt_t m_hc595_opt = {
    do_init,
    do_write,
    do_update,
};

hc595_opt_pt hc595_opt = &m_hc595_opt;
