/**
 * drv_hc595.h — HC595 移位寄存器驱动接口
 *
 * HC595 三级联操作:
 *   1. 串行时钟 (CLK) — 每个上升沿将 DAT 移入移位寄存器
 *   2. 并行锁存 (UD)  — 上升沿将移位寄存器值锁存到输出
 *
 * 两片级联驱动 16 路 LED (每片 8 路):
 *   chip1 (前级) → disp[0] → LEDs 0~7
 *   chip2 (后级) → disp[1] → LEDs 8~15
 *
 * 时序 (MSB 先发):
 *   for each byte (逆序: disp[1] → disp[0]):
 *     for 8 bits (MSB first):
 *       DAT = bit
 *       CLK 0→1 (上升沿移位)
 *   UD 0→1 (锁存输出)
 *
 * 接口指针模式:
 *   hc595_opt → m_hc595_opt → { init, write, update }
 */

#ifndef __DRV_HC595_H__
#define __DRV_HC595_H__

#include <stdint.h>

/* ---- HC595 引脚配置 ---- */
typedef struct {
    int dat;          /* 串行数据引脚 */
    int clk;          /* 移位时钟引脚 */
    int ud;           /* 锁存/更新引脚 */
} hc595_pin_t, *hc595_pin_pt;

/* ---- HC595 操作接口 ---- */
typedef struct {
    void (*init)  (hc595_pin_pt bus);                              /* 初始化引脚 */
    void (*write) (hc595_pin_pt bus, uint8_t *buf, uint8_t len);  /* 写入移位寄存器 */
    void (*update)(hc595_pin_pt bus);                              /* 锁存输出 */
} hc595_opt_t, *hc595_opt_pt;

/* ---- 全局接口指针 ---- */
extern hc595_opt_pt hc595_opt;

#endif /* __DRV_HC595_H__ */
