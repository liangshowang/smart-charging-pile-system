/**
 * led_board.c — LED 灯板业务层实现
 *
 * 16 颗 LED → 2 字节显示缓冲 → HC595 两片级联
 *
 * LED 映射 (V5.4, 低电平有效):
 *   disp[0]:  bits 7..1 → LEDs 7..1 (插座 1, 反向)
 *             bit  0    → LED 0    (网络)
 *   disp[1]:  bits 7    → LED 15   (保险丝)
 *             bits 6..0 → LEDs 14..8 (插座 0)
 *
 * 动画: 7 段跑马灯进度条
 *   每个 work() 周期前进一帧, 位置从 0→6→0 循环。
 */

#include "led_board.h"
#include "drv_hc595.h"
#include "drv_gpio.h"
#include "io_config.h"      /* PIN_HC595_DAT/CLK/UD */
#include <string.h>

/* ---- HC595 总线配置 ---- */
static hc595_pin_t g_led_bus;

/* ---- 显示缓冲 (2 字节, 对应 16 LED) ---- */
static uint8_t disp[2];

/* ---- 每插座 7 颗 LED 的位映射 ---- */
static const uint8_t sock_led_map[2][7] = {
    { 8, 9, 10, 11, 12, 13, 14 },   /* 插座 0: LEDs 8~14 */
    { 7, 6,  5,  4,  3,  2,  1 },    /* 插座 1: LEDs 7~1  (反向) */
};

#define NET_LED    0    /* LED 0:  网络状态 */
#define FUSE_LED  15    /* LED 15: 保险丝状态 */

/* ---- 动画状态 ---- */
static uint8_t sock_mode[2];   /* IDLE/CHARGING/FULL */
static uint8_t sock_pos[2];    /* 当前跑马灯位置 (0~6) */
static uint32_t work_tick;     /* work() 调用计数 */

/* ================================================================
 * set_bit — 设置 disp 中指定位
 *
 * active LOW: on=1 清 bit (灯亮), on=0 置 bit (灯灭)
 * ================================================================ */
static void set_bit(int led, int on)
{
    int idx = (led >= 8) ? 1 : 0;
    int bit = (led >= 8) ? (led - 8) : led;

    if (on)
        disp[idx] &= ~(1 << bit);   /* 清 bit → 低电平 → 灯亮 */
    else
        disp[idx] |=  (1 << bit);   /* 置 bit → 高电平 → 灯灭 */
}

/* ================================================================
 * refresh — 将显示缓冲写入 HC595 并锁存
 * ================================================================ */
static void refresh(void)
{
    hc595_opt->write(&g_led_bus, disp, 2);
    hc595_opt->update(&g_led_bus);
}

/* ================================================================
 * do_init — 初始化灯板
 *
 * 清显示缓冲 → 全灭 → 写 HC595 → 显示初始状态
 * ================================================================ */
static void do_init(void)
{
    /* 绑定引脚: DAT=P1(36), CLK=P0(35), UD=P2(37) */
    g_led_bus.dat = PIN_HC595_DAT;
    g_led_bus.clk = PIN_HC595_CLK;
    g_led_bus.ud  = PIN_HC595_UD;

    /* 全灭 (全 1 = 所有 LED 高电平 = 灭) */
    memset(disp, 0xFF, sizeof(disp));

    /* 初始化动画状态 */
    sock_mode[0] = SOCK_MODE_IDLE;
    sock_mode[1] = SOCK_MODE_IDLE;
    sock_pos[0]  = 0;
    sock_pos[1]  = 0;
    work_tick    = 0;

    /* 初始化 HC595 (配置引脚为输出 + 空闲电平) */
    hc595_opt->init(&g_led_bus);
    refresh();
}

/* ================================================================
 * do_set_net / do_set_fuse — 状态灯控制
 * ================================================================ */
static void do_set_net(int on)
{
    set_bit(NET_LED, on);
    refresh();
}

static void do_set_fuse(int on)
{
    set_bit(FUSE_LED, on);
    refresh();
}

/* ================================================================
 * do_set_sock — 设置插座充电灯动画模式
 * ================================================================ */
static void do_set_sock(int sock, int mode)
{
    if (sock < 0 || sock > 1) return;

    sock_mode[sock] = mode;

    if (mode == SOCK_MODE_IDLE) {
        /* 全灭该插座 LED */
        int k;
        for (k = 0; k < 7; k++)
            set_bit(sock_led_map[sock][k], 0);
        sock_pos[sock] = 0;
        refresh();
    }
    else if (mode == SOCK_MODE_FULL) {
        /* 全亮该插座 LED */
        int k;
        for (k = 0; k < 7; k++)
            set_bit(sock_led_map[sock][k], 1);
        refresh();
    }
    /* CHARGING 由 work() 驱动动画 */
}

/* ================================================================
 * do_work — 动画刷新 (每 ~100ms 调一次)
 *
 * 跑马灯: 每轮前进一格, 7 格一轮回。
 * 只在 CHARGING 模式下更新。
 * ================================================================ */
static void do_work(void)
{
    int sock, k;
    int changed = 0;

    work_tick++;

    for (sock = 0; sock < 2; sock++) {
        if (sock_mode[sock] != SOCK_MODE_CHARGING)
            continue;

        /* 前进一格 (每 ~150ms, work 约每 100ms 调一次) */
        if ((work_tick % 2) == 0)
            continue;

        /* 先灭全部 */
        for (k = 0; k < 7; k++)
            set_bit(sock_led_map[sock][k], 0);

        /* 亮 0..pos (共 pos 颗灯) */
        for (k = 0; k <= sock_pos[sock]; k++)
            set_bit(sock_led_map[sock][k], 1);

        /* 位置前进, 循环 */
        sock_pos[sock]++;
        if (sock_pos[sock] >= 7)
            sock_pos[sock] = 0;

        changed = 1;
    }

    if (changed)
        refresh();
}

/* ================================================================
 * 静态实例 + 导出指针
 *
 * ================================================================ */
static led_board_t m_led_board = {
    do_init,
    do_set_net,
    do_set_fuse,
    do_set_sock,
    do_work,
};

led_board_pt led_board = &m_led_board;
