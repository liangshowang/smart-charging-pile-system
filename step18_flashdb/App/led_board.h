/**
 * led_board.h — LED 灯板业务层接口
 *
 * 硬件: 2 片 HC595 级联驱动 16 颗 LED
 *   LED 0:  网络状态 (Net)
 *   LED 1~7: 插座 1 充电进度条 (Sock1)
 *   LED 8~14: 插座 0 充电进度条 (Sock0)
 *   LED 15: 保险丝状态 (Fuse)
 *
 * LED 低电平有效: 写 0 亮, 写 1 灭。
 *
 * 动画: 充电跑马灯 (7 段进度条)
 *
 * 接口指针模式:
 *   led_board → m_led_board → { init, set_net, set_fuse, set_sock, work }
 */

#ifndef __LED_BOARD_H__
#define __LED_BOARD_H__

#include <stdint.h>

/* ---- 插座编号 ---- */
#define SOCK_0    0
#define SOCK_1    1

/* ---- 插座模式 ---- */
#define SOCK_MODE_IDLE       0   /* 空闲 — 全灭 */
#define SOCK_MODE_CHARGING   1   /* 充电中 — 跑马灯 */
#define SOCK_MODE_FULL       2   /* 充满 — 全亮 */

/* ---- 接口 ---- */
typedef struct {
    void (*init)(void);                              /* 初始化硬件 + LED 全灭 */
    void (*set_net)(int on);                         /* 网络状态灯: 1=亮, 0=灭 */
    void (*set_fuse)(int on);                        /* 保险丝灯: 1=亮, 0=灭 */
    void (*set_sock)(int sock, int mode);            /* 插座充电灯动画模式 */
    void (*work)(void);                              /* 周期性刷新 (驱动动画) */
} led_board_t, *led_board_pt;

/* ---- 全局接口指针 ---- */
extern led_board_pt led_board;

#endif /* __LED_BOARD_H__ */
