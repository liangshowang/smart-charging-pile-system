/**
 * drv_fuse.c — 保险丝检测驱动实现
 *
 * 硬件原理:
 *   220V 50Hz ─→ 光耦 ─→ GPIO (PIN_FUSE0=C2=21)
 *   保险丝正常: 光耦每半个周期导通一次 → 每秒 50 个脉冲
 *   保险丝熔断: 光耦无供电 → 输出恒高或恒低 → 无脉冲
 *
 * 软件逻辑:
 *   1. 开机 2 秒静默: 跳过上电瞬态, 避免误判
 *   2. 5ms 采样去抖: 状态机计数上升沿
 *   3. 每秒判定: pulse < 10 → fuse_err=1 (熔断)
 *                 pulse >=10 → fuse_err=0 (正常)
 *
 * 状态机:
 *   sta=0 → 等 GPIO 为低 → sta=1
 *   sta=1 → 等 GPIO 为高 → pulse++ → sta=0
 *   完整 0→1 跳变 = 一个脉冲 (光耦导通→截止 中检测到高电平)
 */

#include "drv_fuse.h"
#include "drv_gpio.h"
#include "io_config.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- 保险丝错误标志 ---- */
static int fuse_err = 0;

/* ---- 时间戳 (ms) ---- */
static uint32_t otick;        /* 上次采样的 tick */
static uint32_t otick_sec;    /* 上次秒判定的 tick */

/* ---- 脉冲计数 ---- */
static uint32_t pulse;        /* 当前秒内累计脉冲数 */
static uint8_t  sta;          /* 状态机: 0=等低, 1=等高 */
static int      init_done;    /* 初始化标志 */

/* ================================================================
 * do_init — 初始化保险丝检测
 *
 * 配置 fuse0 引脚为输入 (内部上拉, 光耦截止时读高),
 * 清零状态变量。
 * ================================================================ */
static void do_init(void)
{
    drv_gpio->set_mode(PIN_FUSE0, PIN_MODE_INPUT_PULLUP);

    fuse_err  = 0;
    otick     = 0;
    otick_sec = 0;
    pulse     = 50;     /* 初始值 50, 避免上电瞬间误判 */
    sta       = 0;
    init_done = 1;

    sysprt->alog("[fuse] init done, pin=C2(21)\r\n");
}

/* ================================================================
 * do_work — 保险丝轮询检测
 *
 * 每 ~5ms 调用一次 (由 task_listen 驱动)。
 *
 * 流程:
 *   1. 开机 2 秒静默: ntick < 2000 时直接返回
 *   2. 5ms 去抖: 距上次采样不足 5ms 则跳过
 *   3. 状态机: 检测上升沿 (0→1), 计数 pulse++
 *   4. 每秒判定: pulse < 10 → 熔断
 * ================================================================ */
static void do_work(void)
{
    uint32_t ntick = xTaskGetTickCount();
    int val;

    if (!init_done) return;

    /* ---- 开机 2 秒静默: 等待光耦/电源稳定 ---- */
    if (ntick < pdMS_TO_TICKS(2000)) return;

    /* ---- 5ms 采样去抖 ---- */
    if ((ntick - otick) < pdMS_TO_TICKS(5)) return;
    otick = ntick;

    /* ---- 读 GPIO + 状态机计数上升沿 ---- */
    val = drv_gpio->read(PIN_FUSE0);

    switch (sta) {
    case 0:   /* 等低电平 (光耦导通) */
        if (val == 0)
            sta = 1;
        break;

    case 1:   /* 等高电平 (光耦截止, 完成一个脉冲) */
        if (val == 1) {
            sta = 0;
            pulse++;
        }
        break;

    default:
        sta = 0;
        break;
    }

    /* ---- 每秒判定一次 ---- */
    if ((ntick - otick_sec) < pdMS_TO_TICKS(1000)) return;
    otick_sec = ntick;

    if (pulse < 10) {
        /* 脉冲太少 → 保险丝熔断 */
        if (fuse_err == 0) {
            fuse_err = 1;
            sysprt->alog("[fuse] BROKEN! pulse=%lu/sec < 10\r\n", pulse);
        }
    } else {
        /* 脉冲正常 → 保险丝完好 */
        if (fuse_err == 1) {
            fuse_err = 0;
            sysprt->alog("[fuse] recovered, pulse=%lu/sec\r\n", pulse);
        }
    }

    /* 清零, 准备下一个秒周期 */
    pulse = 0;
    sta   = 0;
}

/* ================================================================
 * do_is_err — 查询保险丝状态
 *
 * 返回: 0 = 正常, 1 = 熔断
 * 后续充电状态机会调用此函数, 检测到熔断时立即断开继电器。
 * ================================================================ */
static int do_is_err(void)
{
    return fuse_err;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_fuse_t m_drv_fuse = {
    do_init,
    do_work,
    do_is_err,
};

drv_fuse_pt drv_fuse = &m_drv_fuse;
