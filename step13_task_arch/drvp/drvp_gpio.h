/**
 * drvp_gpio.h — GPIO 端口驱动接口
 *
 * 接口指针模式:
 *   定义一个函数指针结构体 drvp_gpio_t, 全局指针 drvp_gpio 指向其唯一实例。
 *   上层代码只通过 drvp_gpio->xxx() 操作 GPIO, 不直接调 SDK 函数。
 *
 * 引脚编号:
 *   使用 SWM320 物理封装引脚号 (1~64), 内部查表映射到 GPIO 组和组内编号。
 */

#ifndef __DRVP_GPIO_H__
#define __DRVP_GPIO_H__

#include <stdint.h>

/* ---- 引脚电平 ---- */
#define PIN_LOW                  0x00
#define PIN_HIGH                 0x01

/* ---- 引脚方向 / 模式 ---- */
#define PIN_MODE_OUTPUT          0x00
#define PIN_MODE_INPUT           0x01
#define PIN_MODE_INPUT_PULLUP    0x02
#define PIN_MODE_INPUT_PULLDOWN  0x03
#define PIN_MODE_OUTPUT_OD       0x04

/* ---- 中断触发模式 ---- */
#define PIN_IRQ_MODE_RISING       0x00
#define PIN_IRQ_MODE_FALLING      0x01
#define PIN_IRQ_MODE_RISING_FALLING 0x02
#define PIN_IRQ_MODE_HIGH_LEVEL   0x03
#define PIN_IRQ_MODE_LOW_LEVEL    0x04

/* ---- 中断开关 ---- */
#define PIN_IRQ_DISABLE           0x00
#define PIN_IRQ_ENABLE            0x01

/* ---- 回调函数类型 ---- */
typedef void (*pin_callback_t)(void *args);

/* ---- 驱动接口结构体 (虚函数表) ---- */
typedef struct {
    int  (*read)(int pin);
    void (*write)(int pin, int value);
    void (*set_mode)(int pin, int mode);
    int  (*attach_irq)(int pin, int mode, pin_callback_t cb, void *args);
    int  (*detach_irq)(int pin);
    int  (*irq_enable)(int pin, int enabled);
} drvp_gpio_t, *drvp_gpio_pt;

/* ---- 全局接口指针 (由 drvp_gpio.c 定义) ---- */
extern drvp_gpio_pt drvp_gpio;

#endif /* __DRVP_GPIO_H__ */
