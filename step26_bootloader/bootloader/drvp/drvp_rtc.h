/**
 * drvp_rtc.h — RTC 端口驱动接口
 *
 * 封装 SWM320 RTC 外设的初始化、启停、读取。
 *
 * 接口指针模式:
 *   drvp_rtc->init(dt, mINT, sINT)  — 初始化时间 + 中断使能
 *   drvp_rtc->start()               — 启动 RTC
 *   drvp_rtc->stop()                — 停止 RTC (修改时间前必须停)
 *   drvp_rtc->read(dt)              — 读取当前日期时间
 *
 * 中断服务:
 *   定义 void RTC_Handler(void), 覆盖启动文件中 weak 默认。
 *   秒中断 → 刷新缓存时间, 分中断 → 刷新缓存时间。
 */

#ifndef __DRVP_RTC_H__
#define __DRVP_RTC_H__

#include <stdint.h>
#include "SWM320.h"
#include "SWM320_rtc.h"

/* ---- 端口层 RTC 时间缓存 (ISR 中刷新) ---- */
extern RTC_DateTime g_rtc_dt;

/* ---- 接口结构体 ---- */
typedef struct {
    void (*init)(RTC_DateTime *dt, int mINT, int sINT);
    void (*start)(void);
    void (*stop)(void);
    void (*read)(RTC_DateTime *dt);
} drvp_rtc_t, *drvp_rtc_pt;

extern drvp_rtc_pt drvp_rtc;

#endif /* __DRVP_RTC_H__ */
