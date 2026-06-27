/**
 * drvp_rtc.c — RTC 端口驱动实现
 *
 * 直接操作 SWM320 RTC 寄存器。
 *
 * 时钟源: 内部 32KHz RC 振荡器
 * 中断:   可选秒中断/分中断 (用于自动刷新 g_rtc_dt)
 *
 * 纯端口层 — 只封装 MCAL, 不添加业务逻辑。
 */

#include "drvp_rtc.h"
#include "SWM320.h"

/* ---- 全局时间缓存 (ISR 中自动刷新) ---- */
RTC_DateTime g_rtc_dt;

/* ---- 初始化 RTC ----
 *
 * dt:   初始时间
 * mINT: 1 = 使能分中断
 * sINT: 1 = 使能秒中断
 *
 * 内部: 打开 RTC/BKP/ALIVE 时钟, 写入时间寄存器,
 *       配置中断使能。不启动 (需另调 start)。
 * ================================================================ */
static void init(RTC_DateTime *dt, int mINT, int sINT)
{
    RTC_InitStructure initStruct;

    /* 填充初始时间 */
    initStruct.Year   = dt->Year;
    initStruct.Month  = dt->Month;
    initStruct.Date   = dt->Date;
    initStruct.Hour   = dt->Hour;
    initStruct.Minute = dt->Minute;
    initStruct.Second = dt->Second;

    /* 中断使能 */
    initStruct.SecondIEn = (uint8_t)sINT;
    initStruct.MinuteIEn = (uint8_t)mINT;

    /* 调用 MCAL 初始化 (使能时钟 + 写入寄存器) */
    RTC_Init(RTC, &initStruct);
}

/* ---- 启动 RTC ---- */
static void start(void)
{
    RTC_Start(RTC);
}

/* ---- 停止 RTC ---- */
static void stop(void)
{
    RTC_Stop(RTC);
}

/* ---- 读取当前时间 ---- */
static void read(RTC_DateTime *dt)
{
    RTC_GetDateTime(RTC, dt);
}

/* ================================================================
 * RTC_Handler — RTC 中断服务程序
 *
 * 覆盖启动文件中的 weak 默认实现。
 * 秒中断: 刷新 g_rtc_dt
 * 分中断: 刷新 g_rtc_dt
 * ================================================================ */
void RTC_Handler(void)
{
    if (RTC_IntSecondStat(RTC)) {
        RTC_IntSecondClr(RTC);
        RTC_GetDateTime(RTC, &g_rtc_dt);
    }

    if (RTC_IntMinuteStat(RTC)) {
        RTC_IntMinuteClr(RTC);
        RTC_GetDateTime(RTC, &g_rtc_dt);
    }
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drvp_rtc_t m_drvp_rtc = {
    init,
    start,
    stop,
    read,
};

drvp_rtc_pt drvp_rtc = &m_drvp_rtc;
