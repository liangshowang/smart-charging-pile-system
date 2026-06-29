/**
 * drv_buzzer.h — PWM 蜂鸣器驱动接口
 *
 * 通过 PWM 产生方波驱动无源蜂鸣器, 实现不同频率和模式的提示音。
 *
 * 使用:
 *   buzz->init();                          // 初始化 PWM
 *   buzz->beep(BUZZ_BEEP_SHORT);           // 短促提示音
 *   buzz->beep(BUZZ_ALARM);                // 持续报警
 *   buzz->work();                          // 每 tick 调用, 管理时长
 *
 * 蜂鸣模式:
 *   BUZZ_OFF         — 静音
 *   BUZZ_BEEP_SHORT  — 短哔 (100ms, 1000Hz)
 *   BUZZ_BEEP_DOUBLE — 双哔 (100ms+100ms 间隔)
 *   BUZZ_BEEP_LONG   — 长哔 (500ms, 500Hz)
 *   BUZZ_ALARM       — 持续报警 (交替 2kHz/1kHz)
 *   BUZZ_CHARGING    — 充电开始提示 (300ms, 500Hz)
 *   BUZZ_FINISH      — 充电完成 (3声短哔)
 *   BUZZ_ERROR       — 故障报警 (5声短哔)
 */

#ifndef __DRV_BUZZER_H__
#define __DRV_BUZZER_H__

#include <stdint.h>

/* ---- 蜂鸣模式 ---- */
typedef enum {
    BUZZ_OFF = 0,
    BUZZ_BEEP_SHORT,    /* 单次短哔 100ms */
    BUZZ_BEEP_DOUBLE,   /* 双短哔 */
    BUZZ_BEEP_LONG,     /* 单次长哔 500ms */
    BUZZ_ALARM,         /* 持续报警 */
    BUZZ_CHARGING,      /* 充电开始 */
    BUZZ_FINISH,        /* 充电完成 */
    BUZZ_ERROR,         /* 故障 */
} buzz_mode_t;

/* ---- 驱动接口 ---- */
typedef struct {
    /**
     * init — 初始化蜂鸣器 PWM
     * @param freq_hz  默认频率 (典型: 1000)
     * @param duty_pct 占空比百分比 (典型: 50)
     */
    void (*init)(uint32_t freq_hz, uint8_t duty_pct);

    /**
     * beep — 触发蜂鸣模式 (非阻塞, 立即返回)
     * @param mode  蜂鸣模式 (BUZZ_BEEP_SHORT 等)
     */
    void (*beep)(buzz_mode_t mode);

    /**
     * work — 状态机 tick (管理蜂鸣时长和模式切换)
     *        每 50ms 调用一次
     */
    void (*work)(void);

    /** stop — 立即静音 */
    void (*stop)(void);

    /** is_buzzing — 是否正在发声 */
    int (*is_buzzing)(void);
} drv_buzzer_t, *drv_buzzer_pt;

/* ---- 全局接口指针 ---- */
extern drv_buzzer_pt buzz;

#endif
