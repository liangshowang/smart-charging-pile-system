/**
 * drv_gpio.h — GPIO 驱动层接口
 *
 * 这层是平台无关的透传层——直接转发到 drvp_gpio。
 * 上层代码只 include 这个头文件, 不碰 drvp_gpio.h。
 */

#ifndef __DRV_GPIO_H__
#define __DRV_GPIO_H__

#include "drvp_gpio.h"           /* 复用端口层的常量和类型 */

/* ---- 驱动层自己的类型 (与 drvp_gpio_t 签名一致) ---- */
typedef struct {
    int  (*read)(int pin);
    void (*write)(int pin, int value);
    void (*set_mode)(int pin, int mode);
    int  (*attach_irq)(int pin, int mode, pin_callback_t cb, void *args);
    int  (*detach_irq)(int pin);
    int  (*irq_enable)(int pin, int enabled);
} drv_gpio_t, *drv_gpio_pt;

/* ---- 全局接口指针 ---- */
extern drv_gpio_pt drv_gpio;

#endif /* __DRV_GPIO_H__ */
