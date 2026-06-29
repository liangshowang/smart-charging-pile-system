/**
 * drv_buzzer.c — PWM 蜂鸣器驱动实现
 *
 * 硬件:
 *   - 无源蜂鸣器接到 SWM320 PWM 输出引脚
 *   - 三极管驱动 (GPIO 不能直接驱动蜂鸣器)
 *
 * PWM 配置:
 *   - 时钟: 120MHz (VCO) / PWM_CLKDIV_8 = 15MHz
 *   - 频率 = 15MHz / cycle
 *   - 占空比 = hduty / cycle
 *   - 50% 占空比方波 → 最大音量
 *
 * 引脚: 使用 PWM0_CHA (默认为 PORTH PIN1)
 *       如硬件不同, 修改 BUZZ_PWM 和 BUZZ_PORT/BUZZ_PIN/BUZZ_FUNC
 */

#include "SWM320.h"
#include "SWM320_pwm.h"
#include "SWM320_port.h"
#include "drv_buzzer.h"

/* ---- 硬件配置 (按实际接线修改) ---- */
#define BUZZ_PWM       PWM0          /* PWM 模块 */
#define BUZZ_CHANNEL   PWM_CH_A      /* PWM 通道 */
#define BUZZ_PORT      PORTH         /* GPIO 端口 */
#define BUZZ_PIN       PIN1          /* GPIO 引脚 */
#define BUZZ_FUNC      PORTH_PIN1_PWM0_OUTA  /* 复用功能 */

/* ---- PWM 时钟参数 ---- */
#define PWM_CLK_HZ      15000000UL   /* 120MHz / 8 = 15MHz */
#define PWM_DIV          PWM_CLKDIV_8

/* ---- 蜂鸣模式定义 ---- */
typedef struct {
    uint16_t freq_hz;      /* 频率 */
    uint16_t duration_ms;  /* 持续时长 (0=无限) */
    uint8_t  repeat;       /* 重复次数 (0=无限) */
    uint16_t gap_ms;       /* 重复间隔 */
} buzz_pattern_t;

static const buzz_pattern_t g_patterns[] = {
    [BUZZ_OFF]        = {    0,   0, 0,   0 },
    [BUZZ_BEEP_SHORT] = { 1000, 100, 1,   0 },
    [BUZZ_BEEP_DOUBLE]= { 1000, 100, 2, 100 },
    [BUZZ_BEEP_LONG]  = {  500, 500, 1,   0 },
    [BUZZ_ALARM]      = { 2000, 200, 0, 200 },  /* 无限重复, 交替频率 */
    [BUZZ_CHARGING]   = {  500, 300, 1,   0 },
    [BUZZ_FINISH]     = { 1000, 100, 3, 100 },
    [BUZZ_ERROR]      = { 2000, 100, 5, 100 },
};

/* ---- 内部状态 ---- */
static buzz_mode_t   g_mode   = BUZZ_OFF;
static buzz_mode_t   g_queued = BUZZ_OFF;  /* 排队模式 (优先级低于当前) */
static uint32_t      g_tick_ms = 0;
static uint32_t      g_stop_at = 0;
static uint8_t       g_repeat_left = 0;
static uint8_t       g_in_gap = 0;
static uint32_t      g_duty_pct = 50;

/* ---- 前向声明 ---- */
static void do_init(uint32_t freq_hz, uint8_t duty_pct);
static void do_beep(buzz_mode_t mode);
static void do_work(void);
static void do_stop(void);
static int  do_is_buzzing(void);
static void pwm_set_freq(uint32_t freq_hz);

/* ================================================================
 * do_init — 初始化 PWM 蜂鸣器
 * ================================================================ */
static void do_init(uint32_t freq_hz, uint8_t duty_pct)
{
    PWM_InitStructure pis;

    g_duty_pct = (duty_pct > 100) ? 50 : duty_pct;

    /* 配置引脚为 PWM 输出 */
    PORT_Init(BUZZ_PORT, BUZZ_PIN, BUZZ_FUNC, 0);

    /* 初始化 PWM */
    pis.clk_div    = PWM_DIV;
    pis.mode       = PWM_MODE_INDEP;
    pis.cycleA     = (uint16_t)(PWM_CLK_HZ / freq_hz);
    pis.hdutyA     = (uint16_t)(pis.cycleA * g_duty_pct / 100);
    pis.deadzoneA  = 0;
    pis.initLevelA = 0;
    /* B 通道不使用 */
    pis.cycleB     = 0;
    pis.hdutyB     = 0;
    pis.deadzoneB  = 0;
    pis.initLevelB = 0;
    pis.HEndAIEn   = 0;
    pis.NCycleAIEn = 0;
    pis.HEndBIEn   = 0;
    pis.NCycleBIEn = 0;

    PWM_Init(BUZZ_PWM, &pis);
}

/* ================================================================
 * pwm_set_freq — 修改 PWM 频率
 * ================================================================ */
static void pwm_set_freq(uint32_t freq_hz)
{
    uint16_t cycle;

    if (freq_hz == 0) {
        PWM_Stop(BUZZ_PWM, 1, 0);
        return;
    }

    /* 频率范围: 20Hz ~ 20kHz */
    if (freq_hz < 20)  freq_hz = 20;
    if (freq_hz > 20000) freq_hz = 20000;

    cycle = (uint16_t)(PWM_CLK_HZ / freq_hz);

    PWM_SetCycle(BUZZ_PWM, BUZZ_CHANNEL, cycle);
    PWM_SetHDuty(BUZZ_PWM, BUZZ_CHANNEL, (uint16_t)((uint32_t)cycle * g_duty_pct / 100));
}

/* ================================================================
 * do_beep — 触发蜂鸣
 *
 * 优先级: 故障 > 持续报警 > 简单提示音
 * 当前正在发声且优先级更高时, 排队等待
 * ================================================================ */
static void do_beep(buzz_mode_t mode)
{
    if (mode == BUZZ_OFF) {
        do_stop();
        return;
    }

    if (g_mode == BUZZ_OFF) {
        /* 空闲 — 立即开始 */
        g_mode = mode;
        g_tick_ms = 0;
        g_stop_at = g_patterns[mode].duration_ms;
        g_repeat_left = g_patterns[mode].repeat;
        g_in_gap = 0;

        pwm_set_freq(g_patterns[mode].freq_hz);
        PWM_Start(BUZZ_PWM, 1, 0);
    } else {
        /* 正在发声 — 排队 (简单覆盖) */
        g_queued = mode;
    }
}

/* ================================================================
 * do_work — 蜂鸣状态机 (每 50ms 调用)
 *
 * 管理:
 *   1. 单次蜂鸣到时 → 停止
 *   2. 重复蜂鸣 → 间隔后再次触发
 *   3. 持续报警 → 交替频率
 *   4. 完成后检查排队
 * ================================================================ */
static void do_work(void)
{
    const buzz_pattern_t *pat;

    if (g_mode == BUZZ_OFF) {
        /* 检查排队 */
        if (g_queued != BUZZ_OFF) {
            buzz_mode_t next = g_queued;
            g_queued = BUZZ_OFF;
            do_beep(next);
        }
        return;
    }

    pat = &g_patterns[g_mode];
    g_tick_ms += 50;

    if (g_in_gap) {
        /* 间隔阶段 */
        if (g_tick_ms >= pat->gap_ms) {
            g_tick_ms = 0;
            g_in_gap = 0;
            g_stop_at = pat->duration_ms;
            pwm_set_freq(pat->freq_hz);
            PWM_Start(BUZZ_PWM, 1, 0);
        }
        return;
    }

    /* 发声阶段 */
    if (pat->duration_ms > 0 && g_tick_ms >= g_stop_at) {
        /* 该段到时 */
        if (g_repeat_left > 1) {
            /* 还有剩余次数 */
            g_repeat_left--;
            g_tick_ms = 0;
            g_in_gap = 1;
            PWM_Stop(BUZZ_PWM, 1, 0);
        } else if (g_repeat_left == 0) {
            /* 无限重复: 交替频率 (BUZZ_ALARM 特效) */
            g_tick_ms = 0;
            g_in_gap = 1;
            PWM_Stop(BUZZ_PWM, 1, 0);
        } else {
            /* 完成 */
            PWM_Stop(BUZZ_PWM, 1, 0);
            g_mode = BUZZ_OFF;
            g_queued = BUZZ_OFF;
        }
    }
}

/* ================================================================
 * do_stop — 立即静音
 * ================================================================ */
static void do_stop(void)
{
    PWM_Stop(BUZZ_PWM, 1, 0);
    g_mode = BUZZ_OFF;
    g_queued = BUZZ_OFF;
    g_tick_ms = 0;
    g_in_gap = 0;
}

/* ================================================================
 * do_is_buzzing — 查询是否正在发声
 * ================================================================ */
static int do_is_buzzing(void)
{
    return (g_mode != BUZZ_OFF) ? 1 : 0;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_buzzer_t m_buzzer = {
    do_init,
    do_beep,
    do_work,
    do_stop,
    do_is_buzzing,
};

drv_buzzer_pt buzz = &m_buzzer;
