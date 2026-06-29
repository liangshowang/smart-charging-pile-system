/**
 * drv_wdt.c — 看门狗驱动实现
 *
 * 配置:
 *   - 超时: 默认 5000ms (5秒)
 *   - 模式: WDT_MODE_RESET (超时后复位 MCU)
 *   - 喂狗: 由 ctrl 任务每 2 秒执行一次
 *
 * 注意:
 *   - OTA Phase 6 (FINISH) 中不喂狗, 靠 WDT 复位切换回 APP
 *   - 调试时可调用 wdt->disable() 防止断点时复位
 */

#include "SWM320.h"
#include "SWM320_wdt.h"
#include "drv_wdt.h"

/* ---- 内部状态 ---- */
static uint32_t g_timeout_ms = 5000;

/* ================================================================
 * do_init — 初始化 WDT
 *
 * WDT 时钟: HRC (12MHz) / 32 = 375kHz = 375 ticks/ms
 * 周期 = timeout_ms * 375
 * ================================================================ */
static void do_init(uint32_t timeout_ms)
{
    uint32_t period;

    g_timeout_ms = timeout_ms;
    period = timeout_ms * (12000000 / 32) / 1000;

    WDT_Init(WDT, period, WDT_MODE_RESET);
    WDT_Start(WDT);
}

/* ================================================================
 * do_feed — 喂狗
 * ================================================================ */
static void do_feed(void)
{
    WDT_Feed(WDT);
}

/* ================================================================
 * do_disable — 禁用 WDT (调试用)
 * ================================================================ */
static void do_disable(void)
{
    WDT_Stop(WDT);
}

/* ================================================================
 * do_get_value — 读当前计数值
 * ================================================================ */
static uint32_t do_get_value(void)
{
    return (uint32_t)WDT_GetValue(WDT);
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_wdt_t m_wdt = {
    do_init,
    do_feed,
    do_disable,
    do_get_value,
};

drv_wdt_pt wdt = &m_wdt;
