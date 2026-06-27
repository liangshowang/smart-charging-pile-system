/**
 * drvp_gpio.c — GPIO 端口驱动实现
 *
 * 核心数据结构: swm32_pin_map[] — SWM320RET7 全 64 引脚映射表
 *
 *   物理封装引脚号 → { GPIO 组地址, 组内引脚号, 中断号 }
 *   无效引脚 (电源/GND/空) 用 GPIO0 标记, get_pin() 返回 NULL。
 *
 * 架构:
 *   drvp_gpio (全局指针) → m_drvp_gpio (静态实例)
 *     → { read, write, set_mode, attach_irq, detach_irq, irq_enable }
 *       → get_pin(pin) → 查表 → SDK 函数
 *
 * 中断处理:
 *   6 组 ISR (GPIOA/B/C/M/N/P_Handler) — 遍历本组引脚 → 回调用户注册的函数
 */

#include "SWM320.h"
#include "drvp_gpio.h"
#include <stddef.h>

/* ================================================================
 * 引脚描述表
 * ================================================================ */
struct pin_desc {
    uint32_t      pkg_index;    /* 封装引脚号 (1~64) */
    const char   *name;         /* 引脚名 (如 "M2", "B12") */
    GPIO_TypeDef *port;         /* GPIO 组基址 (GPIOA/B/C/M/N/P), GPIO0=无效 */
    uint32_t      grp_pin;      /* 组内引脚号 */
    IRQn_Type     irq;          /* 中断号 */
    pin_callback_t callback;    /* 用户回调 */
    void         *cb_args;      /* 回调参数 */
};

/* GPIO0: 伪 GPIO 组, 标记无效引脚 */
#define GPIO0      ((GPIO_TypeDef *)0)
#define GPIO0_IRQn (GPIOA0_IRQn)

/* 构造一行引脚描述 */
#define PIN_ENTRY(idx, nm, grp, pin_num) \
    { idx, #nm, GPIO##grp, pin_num, GPIO##grp##_IRQn, NULL, NULL }

#define PIN_INVALID(idx, nm) \
    { idx, #nm, GPIO0, 0, GPIO0_IRQn, NULL, NULL }

/* 完整的 64 引脚映射 */
static struct pin_desc swm32_pin_map[] = {
    PIN_INVALID( 0, None  ),
    PIN_INVALID( 1, 3V3   ),   PIN_INVALID( 2, CAP0  ),   PIN_ENTRY( 3, B12  , B, 12),
    PIN_INVALID( 4, 3V3   ),   PIN_INVALID( 5, 3V3   ),   PIN_INVALID( 6, GND  ),
    PIN_INVALID( 7, CAP2  ),   PIN_ENTRY( 8, N9   , N,  9),   PIN_ENTRY( 9, N10 , N, 10),
    PIN_INVALID(10, CAP1  ),   PIN_INVALID(11, GND   ),   PIN_INVALID(12, 3V3  ),
    PIN_ENTRY(13, N2    , N,  2),  PIN_ENTRY(14, N1   , N,  1),  PIN_ENTRY(15, N0  , N,  0),
    PIN_INVALID(16, 3V3   ),
    PIN_ENTRY(17, C4    , C,  4),  PIN_ENTRY(18, C5   , C,  5),  PIN_ENTRY(19, C6  , C,  6),
    PIN_ENTRY(20, C7    , C,  7),  PIN_ENTRY(21, C2   , C,  2),  PIN_ENTRY(22, C3  , C,  3),
    PIN_INVALID(23, XI    ),   PIN_INVALID(24, XO    ),   PIN_INVALID(25, RESET),
    PIN_ENTRY(26, M2    , M,  2),  PIN_ENTRY(27, M3   , M,  3),  PIN_ENTRY(28, M4  , M,  4),
    PIN_ENTRY(29, M5    , M,  5),  PIN_ENTRY(30, M6   , M,  6),  PIN_ENTRY(31, M7  , M,  7),
    PIN_INVALID(32, GND   ),
    PIN_ENTRY(33, M1    , M,  1),  PIN_ENTRY(34, M0   , M,  0),  PIN_ENTRY(35, P0  , P,  0),
    PIN_ENTRY(36, P1    , P,  1),  PIN_ENTRY(37, P2   , P,  2),  PIN_ENTRY(38, P3  , P,  3),
    PIN_ENTRY(39, P4    , P,  4),  PIN_ENTRY(40, P5   , P,  5),  PIN_ENTRY(41, P6  , P,  6),
    PIN_ENTRY(42, P7    , P,  7),  PIN_ENTRY(43, P8   , P,  8),  PIN_ENTRY(44, P9  , P,  9),
    PIN_ENTRY(45, P10   , P, 10),  PIN_ENTRY(46, P11  , P, 11),  PIN_ENTRY(47, P12 , P, 12),
    PIN_ENTRY(48, P13   , P, 13),  PIN_ENTRY(49, B0   , B,  0),  PIN_ENTRY(50, A0  , A,  0),
    PIN_ENTRY(51, A1    , A,  1),  PIN_ENTRY(52, A2   , A,  2),  PIN_ENTRY(53, A3  , A,  3),
    PIN_ENTRY(54, A4    , A,  4),  PIN_ENTRY(55, A5   , A,  5),  PIN_ENTRY(56, N7  , N,  7),
    PIN_ENTRY(57, N6    , N,  6),  PIN_ENTRY(58, N5   , N,  5),  PIN_ENTRY(59, N4  , N,  4),
    PIN_ENTRY(60, N3    , N,  3),  PIN_ENTRY(61, A9   , A,  9),  PIN_ENTRY(62, A10 , A, 10),
    PIN_ENTRY(63, A11   , A, 11),  PIN_ENTRY(64, A12  , A, 12),
};

#define ITEM_NUM(items)  (sizeof(items) / sizeof((items)[0]))

/* ================================================================
 * get_pin — 查表, 无效引脚返回 NULL
 * ================================================================ */
static struct pin_desc *get_pin(int pin)
{
    if (pin < 0 || pin >= (int)ITEM_NUM(swm32_pin_map))
        return NULL;

    struct pin_desc *p = &swm32_pin_map[pin];
    if (p->port == GPIO0)
        return NULL;

    return p;
}

/* ================================================================
 * read — 读引脚电平
 * ================================================================ */
static int drvp_read(int pin)
{
    struct pin_desc *p = get_pin(pin);
    if (p == NULL) return 0;
    return GPIO_GetBit(p->port, p->grp_pin);
}

/* ================================================================
 * write — 写引脚电平
 * ================================================================ */
static void drvp_write(int pin, int value)
{
    struct pin_desc *p = get_pin(pin);
    if (p == NULL) return;

    if (value)
        GPIO_SetBit(p->port, p->grp_pin);
    else
        GPIO_ClrBit(p->port, p->grp_pin);
}

/* ================================================================
 * set_mode — 配置引脚方向 (输入/输出/上下拉/开漏)
 * ================================================================ */
static void drvp_set_mode(int pin, int mode)
{
    struct pin_desc *p = get_pin(pin);
    int dir = 0, pull_up = 0, pull_down = 0;

    if (p == NULL) return;

    switch (mode) {
    case PIN_MODE_OUTPUT:
        dir = 1;
        break;
    case PIN_MODE_INPUT:
        dir = 0;
        break;
    case PIN_MODE_INPUT_PULLUP:
        dir = 0; pull_up = 1;
        break;
    case PIN_MODE_INPUT_PULLDOWN:
        dir = 0; pull_down = 1;
        break;
    case PIN_MODE_OUTPUT_OD:
        dir = 1; pull_up = 0;
        break;
    default:
        return;
    }
    GPIO_Init(p->port, p->grp_pin, dir, pull_up, pull_down);
}

/* ================================================================
 * attach_irq — 注册中断回调
 * ================================================================ */
static int drvp_attach_irq(int pin, int mode, pin_callback_t cb, void *args)
{
    struct pin_desc *p = get_pin(pin);
    if (p == NULL) return -1;

    p->callback = cb;
    p->cb_args  = args;
    /* mode 在 irq_enable 时使用 */
    (void)mode;
    return 0;
}

/* ================================================================
 * detach_irq — 注销中断回调
 * ================================================================ */
static int drvp_detach_irq(int pin)
{
    struct pin_desc *p = get_pin(pin);
    if (p == NULL) return -1;

    p->callback = NULL;
    p->cb_args  = NULL;
    return 0;
}

/* ================================================================
 * irq_enable — 配置 EXTI 触发模式并使能/关闭中断
 * ================================================================ */
static int drvp_irq_enable(int pin, int enabled)
{
    struct pin_desc *p = get_pin(pin);
    if (p == NULL) return -1;

    if (enabled == PIN_IRQ_ENABLE) {
        /* 按触发模式配置 */
        switch ((int)(uintptr_t)p->cb_args) {
        case PIN_IRQ_MODE_RISING:
            GPIO_Init(p->port, p->grp_pin, 0, 0, 1);
            EXTI_Init(p->port, p->grp_pin, EXTI_RISE_EDGE);
            break;
        case PIN_IRQ_MODE_FALLING:
            GPIO_Init(p->port, p->grp_pin, 0, 1, 0);
            EXTI_Init(p->port, p->grp_pin, EXTI_FALL_EDGE);
            break;
        case PIN_IRQ_MODE_RISING_FALLING:
            GPIO_Init(p->port, p->grp_pin, 0, 1, 1);
            EXTI_Init(p->port, p->grp_pin, EXTI_BOTH_EDGE);
            break;
        case PIN_IRQ_MODE_HIGH_LEVEL:
            GPIO_Init(p->port, p->grp_pin, 0, 0, 1);
            EXTI_Init(p->port, p->grp_pin, EXTI_HIGH_LEVEL);
            break;
        case PIN_IRQ_MODE_LOW_LEVEL:
            GPIO_Init(p->port, p->grp_pin, 0, 1, 0);
            EXTI_Init(p->port, p->grp_pin, EXTI_LOW_LEVEL);
            break;
        default:
            return -1;
        }
        NVIC_EnableIRQ(p->irq);
        EXTI_Open(p->port, p->grp_pin);
    } else {
        NVIC_DisableIRQ(p->irq);
        EXTI_Close(p->port, p->grp_pin);
    }
    return 0;
}

/* ================================================================
 * 静态实例 + 导出指针
 *
 *   m_drvp_gpio: 静态结构体, 6 个函数指针指向上面的实现
 *   drvp_gpio  : 全局指针, 指向 m_drvp_gpio, 上层唯一入口
 * ================================================================ */
static drvp_gpio_t m_drvp_gpio = {
    drvp_read,
    drvp_write,
    drvp_set_mode,
    drvp_attach_irq,
    drvp_detach_irq,
    drvp_irq_enable,
};

drvp_gpio_pt drvp_gpio = &m_drvp_gpio;

/* ================================================================
 * GPIO 组中断处理 (6 组)
 *
 * 每组 ISR 首次调用时建索引: 找出本组所有有效引脚。
 * 之后每次中断遍历索引, 查到触发者 → 调用户回调。
 * ================================================================ */
#define MAX_PER_GROUP 14

/* 公共处理模板 */
static void gpio_group_isr(GPIO_TypeDef *target_port)
{
    static int      gpio_index[6][MAX_PER_GROUP];  /* 各组引脚索引 */
    static int      init_done[6] = {0};
    int             group_id;
    struct pin_desc *p;
    int             i;

    /* 确定组 ID */
    if      (target_port == GPIOA) group_id = 0;
    else if (target_port == GPIOB) group_id = 1;
    else if (target_port == GPIOC) group_id = 2;
    else if (target_port == GPIOM) group_id = 3;
    else if (target_port == GPION) group_id = 4;
    else if (target_port == GPIOP) group_id = 5;
    else return;

    /* 首次调用: 建索引 */
    if (init_done[group_id] == 0) {
        init_done[group_id] = 1;
        int idx = 0;
        for (i = 1; i < (int)ITEM_NUM(swm32_pin_map); i++) {
            p = &swm32_pin_map[i];
            if (p->port == target_port) {
                gpio_index[group_id][idx++] = p->pkg_index;
                if (idx >= MAX_PER_GROUP) break;
            }
        }
    }

    /* 遍历本组引脚, 查谁触发 */
    for (i = 0; i < MAX_PER_GROUP; i++) {
        int pin_num = gpio_index[group_id][i];
        if (pin_num == 0) continue;

        p = get_pin(pin_num);
        if (p == NULL) continue;

        if (EXTI_State(p->port, p->grp_pin)) {
            EXTI_Clear(p->port, p->grp_pin);
            if (p->callback) {
                p->callback(p->cb_args);
            }
        }
    }
}

void GPIOA_Handler(void) { gpio_group_isr(GPIOA); }
void GPIOB_Handler(void) { gpio_group_isr(GPIOB); }
void GPIOC_Handler(void) { gpio_group_isr(GPIOC); }
void GPIOM_Handler(void) { gpio_group_isr(GPIOM); }
void GPION_Handler(void) { gpio_group_isr(GPION); }
void GPIOP_Handler(void) { gpio_group_isr(GPIOP); }
