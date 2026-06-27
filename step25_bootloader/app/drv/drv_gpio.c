/**
 * drv_gpio.c — GPIO 驱动层实现
 *
 * 纯透传: 每个函数直接调用 drvp_gpio 的对应方法。
 * 换芯片时, 只需换 drvp_gpio, 本文件零改动。
 */

#include "drv_gpio.h"

/* ---- 透传函数 ---- */
static int  do_read(int pin)                              { return drvp_gpio->read(pin); }
static void do_write(int pin, int value)                  { drvp_gpio->write(pin, value); }
static void do_set_mode(int pin, int mode)                { drvp_gpio->set_mode(pin, mode); }
static int  do_attach_irq(int pin, int m, pin_callback_t cb, void *a) { return drvp_gpio->attach_irq(pin, m, cb, a); }
static int  do_detach_irq(int pin)                        { return drvp_gpio->detach_irq(pin); }
static int  do_irq_enable(int pin, int en)                { return drvp_gpio->irq_enable(pin, en); }

/* ---- 静态实例 + 导出指针 ---- */
static drv_gpio_t m_drv_gpio = {
    do_read, do_write, do_set_mode,
    do_attach_irq, do_detach_irq, do_irq_enable,
};

drv_gpio_pt drv_gpio = &m_drv_gpio;
