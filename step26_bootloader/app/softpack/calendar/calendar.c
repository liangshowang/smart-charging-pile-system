/**
 * calendar.c — 时间管理模块实现
 *
 * 核心功能:
 *   - RTC 硬件管理 (通过 drvp_rtc)
 *   - 服务器时间同步 ("YYYYMMDDhhmmss" → RTC)
 *   - 时间工具函数
 *   - 分时电价时段存储和查找
 *
 * 纯软件模块 — 通过 drvp_rtc 操作硬件。
 */

#include "calendar.h"
#include "drvp_rtc.h"
#include "sysprt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 内部状态 ---- */
static RTC_DateTime g_cached_dt;        /* 缓存时间 */
static slot_t       g_slots[MAX_SLOTS]; /* 时段表 */
static int          g_slot_count = 0;   /* 当前时段数 */

/* ---- 默认初始时间: 2020-01-01 00:00:00 ---- */
#define DEFAULT_YEAR   2020
#define DEFAULT_MONTH  1
#define DEFAULT_DATE   1
#define DEFAULT_HOUR   0
#define DEFAULT_MINUTE 0
#define DEFAULT_SECOND 0

/* ================================================================
 * do_init — 初始化 RTC + 缓存时间
 *
 * 首次上电时 RTC 无有效时间, 写入默认值。
 * 服务器 time 命令会同步为正确时间。
 * ================================================================ */
static void do_init(void)
{
    RTC_DateTime dt;

    /* 填充默认时间 */
    dt.Year   = DEFAULT_YEAR;
    dt.Month  = DEFAULT_MONTH;
    dt.Date   = DEFAULT_DATE;
    dt.Hour   = DEFAULT_HOUR;
    dt.Minute = DEFAULT_MINUTE;
    dt.Second = DEFAULT_SECOND;
    dt.Day    = 0;

    /* 初始化 RTC: 使能分中断 (每分钟自动刷新缓存) */
    drvp_rtc->init(&dt, 1, 0);
    drvp_rtc->start();

    /* 初始缓存 */
    memcpy(&g_cached_dt, &dt, sizeof(RTC_DateTime));

    sysprt->alog("[calendar] RTC initialized: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                 dt.Year, dt.Month, dt.Date,
                 dt.Hour, dt.Minute, dt.Second);
}

/* ================================================================
 * do_work — 刷新缓存时间
 *
 * 从 g_rtc_dt (ISR 自动更新) 或直接读 RTC 复制到 g_cached_dt。
 * ================================================================ */
static void do_work(void)
{
    /* 如果启用了 RTC 中断, g_rtc_dt 已被 ISR 自动更新 */
    memcpy(&g_cached_dt, &g_rtc_dt, sizeof(RTC_DateTime));
}

/* ================================================================
 * do_sync_by_str — 解析服务器时间并设置 RTC
 *
 * 输入: "20260625143000" (YYYYMMDDhhmmss, 14 位)
 *
 * 流程:
 *   1. 逐字段解析 (year/month/date/hour/min/second)
 *   2. 停止 RTC
 *   3. 写入新时间
 *   4. 启动 RTC
 *   5. 更新缓存
 * ================================================================ */
static int do_sync_by_str(char *str)
{
    uint32_t year, month, date, hour, minute, second;
    char tbuf[8];
    RTC_DateTime dt;

    if (str == NULL) return -1;
    if (strlen(str) < 14) return -1;

    /* 逐字段解析 (每字段先拷到临时 buf 再 atol) */

    /* YYYY — 位置 0-3 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[0]; tbuf[1] = str[1];
    tbuf[2] = str[2]; tbuf[3] = str[3];
    year = atol(tbuf);

    /* MM — 位置 4-5 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[4]; tbuf[1] = str[5];
    month = atol(tbuf);

    /* DD — 位置 6-7 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[6]; tbuf[1] = str[7];
    date = atol(tbuf);

    /* hh — 位置 8-9 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[8]; tbuf[1] = str[9];
    hour = atol(tbuf);

    /* mm — 位置 10-11 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[10]; tbuf[1] = str[11];
    minute = atol(tbuf);

    /* ss — 位置 12-13 */
    memset(tbuf, 0, sizeof(tbuf));
    tbuf[0] = str[12]; tbuf[1] = str[13];
    second = atol(tbuf);

    /* 基本合法性检查 */
    if (month < 1 || month > 12)  return -1;
    if (date  < 1 || date  > 31)  return -1;
    if (hour  > 23 || minute > 59 || second > 59) return -1;

    /* 填充 RTC 时间结构 */
    dt.Year   = (uint16_t)year;
    dt.Month  = (uint8_t)month;
    dt.Date   = (uint8_t)date;
    dt.Day    = 0;
    dt.Hour   = (uint8_t)hour;
    dt.Minute = (uint8_t)minute;
    dt.Second = (uint8_t)second;

    /* 写入 RTC: 停止 → 初始化 → 启动 */
    drvp_rtc->stop();
    drvp_rtc->init(&dt, 1, 0);   /* 保持分中断使能 */
    drvp_rtc->start();

    /* 更新缓存 */
    memcpy(&g_cached_dt, &dt, sizeof(RTC_DateTime));

    sysprt->alog("[calendar] time synced: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                 year, month, date, hour, minute, second);

    return 0;
}

/* ---- 获取缓存时间 ---- */
static void do_get_dt(RTC_DateTime *dt)
{
    if (dt != NULL) {
        memcpy(dt, &g_cached_dt, sizeof(RTC_DateTime));
    }
}

/* ---- 直接设置 RTC 时间 ---- */
static void do_set_dt(RTC_DateTime *dt)
{
    if (dt == NULL) return;

    drvp_rtc->stop();
    drvp_rtc->init(dt, 1, 0);
    drvp_rtc->start();

    memcpy(&g_cached_dt, dt, sizeof(RTC_DateTime));
}

/* ================================================================
 * do_to_minutes — HH:MM → 当日分钟数
 * ================================================================ */
static int do_to_minutes(uint8_t hour, uint8_t min)
{
    return ((int)hour) * 60 + (int)min;
}

/* ================================================================
 * do_get_time_segments — 查找当前时间属于哪个时段
 *
 * list: 时段表 (按 start 升序)
 * len:  时段数
 * hour, min: 当前时间
 *
 * 返回时段索引 (0-based), -1 = 未匹配
 * ================================================================ */
static int do_get_time_segments(slot_t *list, int len, int hour, int min)
{
    int i;
    int time = hour * 60 + min;

    for (i = 0; i < len; i++) {
        if (list[i].start <= time && time <= list[i].end) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 * do_calculate_overlap — 计算两个时段的重叠分钟数
 *
 * 用途: 充电订单可能横跨多个计费时段,
 *       算出在每个时段内充了多少分钟, 用于分时计费。
 *
 * period0: 计费时段 (固定, 如 00:00-08:00)
 * period1: 充电时段 (实际, 如 06:30-09:30)
 *
 * 处理跨天: period1.start > period0.end 时,
 *           period0 自动 +1440 分钟 (推到第二天)
 * ================================================================ */
static int do_calculate_overlap(slot_t period0, slot_t period1)
{
    int p_start = period0.start;
    int p_end   = period0.end;
    int t_start = period1.start;
    int t_end   = period1.end;
    int overlap_start, overlap_end;

    /* 如果充电开始时间在计费时段结束之后,
     * 把计费时段推到第二天 (有可能跨天) */
    if (t_start > p_end) {
        p_start += 24 * 60;
        p_end   += 24 * 60;
    }

    /* 无重叠 */
    if (p_end <= t_start || p_start >= t_end) {
        return 0;
    }

    /* 取重叠区间 */
    overlap_start = (p_start > t_start) ? p_start : t_start;
    overlap_end   = (p_end   < t_end)   ? p_end   : t_end;

    return overlap_end - overlap_start;
}

/* ================================================================
 * do_datetime_to_minutes — 日期时间 → 绝对分钟数
 *
 * 从 0000-01-01 起算的总分钟数。
 * 用于计算两个时间之间的差值 (如订单结束时间 - 当前时间)。
 *
 * 注意: 这是一个近似计算, 忽略 1582 年历法改革等历史细节,
 *       对于充电桩的时间差计算足够准确。
 * ================================================================ */
static uint32_t do_datetime_to_minutes(RTC_DateTime dt)
{
    uint32_t total = 0;
    int year, month;

    /* 年份贡献 */
    for (year = 0; (uint16_t)year < dt.Year; year++) {
        total += 365 * 24 * 60;
        /* 闰年多加一天 */
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            total += 24 * 60;
        }
    }

    /* 月份贡献 */
    for (month = 1; month < (int)dt.Month; month++) {
        /* 每月天数 */
        uint8_t dim;
        if (month == 2) {
            dim = ((dt.Year % 4 == 0 && dt.Year % 100 != 0)
                   || (dt.Year % 400 == 0)) ? 29 : 28;
        } else if (month == 4 || month == 6 || month == 9 || month == 11) {
            dim = 30;
        } else {
            dim = 31;
        }
        total += dim * 24 * 60;
    }

    /* 日期/小时/分钟贡献 */
    total += (dt.Date - 1) * 24 * 60;
    total += dt.Hour * 60;
    total += dt.Minute;

    return total;
}

/* ================================================================
 * do_add_minutes — 日期时间加分钟
 *
 * 正确处理进位: 分钟→小时→日期→月份→年份。
 * 使用内部 dt_t 结构避免 uint8_t 溢出问题。
 * ================================================================ */
static void do_add_minutes(RTC_DateTime *dt, int add_minutes)
{
    int year, month, date, hour, minute, second;
    int dim; /* days in month */

    if (dt == NULL || add_minutes <= 0) return;

    /* 展开到 int 避免 uint8_t 溢出 */
    year   = dt->Year;
    month  = dt->Month;
    date   = dt->Date;
    hour   = dt->Hour;
    minute = dt->Minute;
    second = dt->Second;

    /* 加分钟 */
    minute += add_minutes;

    /* 进位到小时 */
    while (minute >= 60) {
        minute -= 60;
        hour++;
    }

    /* 进位到日期 */
    while (hour >= 24) {
        hour -= 24;
        date++;
    }

    /* 进位到月份 */
    while (1) {
        if (month == 2) {
            dim = ((year % 4 == 0 && year % 100 != 0)
                   || (year % 400 == 0)) ? 29 : 28;
        } else if (month == 4 || month == 6 || month == 9 || month == 11) {
            dim = 30;
        } else {
            dim = 31;
        }

        if (date <= dim) break;

        date -= dim;
        month++;
        if (month > 12) {
            month = 1;
            year++;
        }
    }

    /* 写回 */
    dt->Year   = (uint16_t)year;
    dt->Month  = (uint8_t)month;
    dt->Date   = (uint8_t)date;
    dt->Hour   = (uint8_t)hour;
    dt->Minute = (uint8_t)minute;
    dt->Second = (uint8_t)second;
}

/* ================================================================
 * do_parse_jfpg — 解析 jfpg 时段字符串
 *
 * 输入: argv[0..argc-1], 每个是 "HH:MM-HH:MM" 格式
 *       最后一个参数是 checksum (数字, 跳过)
 *
 * 示例: jfpg 00:00-08:00 08:00-10:00 10:00-12:00 ... 120
 *
 * 存储到 g_slots[], group = 索引, start/end = 分钟数
 * ================================================================ */
static int do_parse_jfpg(int argc, char **argv)
{
    int i;
    int count = 0;

    if (argc < 2 || argv == NULL) return 0;

    /* 最后一个参数是 checksum (纯数字), 跳过 */
    {
        char *last = argv[argc - 1];
        if (last[0] >= '0' && last[0] <= '9'
            && strchr(last, '-') == NULL) {
            argc--;  /* 排除 checksum */
        }
    }

    for (i = 0; i < argc && count < MAX_SLOTS; i++) {
        int start_h, start_m, end_h, end_m;
        char *p = argv[i];

        /* 解析 "HH:MM-HH:MM" */
        if (sscanf(p, "%2d:%2d-%2d:%2d",
                   &start_h, &start_m, &end_h, &end_m) == 4) {

            g_slots[count].group = count;  /* 每组一个价格 */
            g_slots[count].start = start_h * 60 + start_m;
            g_slots[count].end   = end_h   * 60 + end_m;

            sysprt->alog("[calendar] slot %d: %02d:%02d-%02d:%02d "
                         "(group=%d)\r\n",
                         count, start_h, start_m, end_h, end_m,
                         g_slots[count].group);
            count++;
        }
    }

    g_slot_count = count;
    sysprt->alog("[calendar] jfpg parsed: %d slots\r\n", count);

    return count;
}

/* ---- 获取已存储的时段列表 ---- */
static int do_get_slots(slot_t **slots)
{
    if (slots != NULL) {
        *slots = g_slots;
    }
    return g_slot_count;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static calendar_t m_calendar = {
    do_init,
    do_work,
    do_sync_by_str,
    do_get_dt,
    do_set_dt,
    do_to_minutes,
    do_get_time_segments,
    do_calculate_overlap,
    do_datetime_to_minutes,
    do_add_minutes,
    do_parse_jfpg,
    do_get_slots,
};

calendar_pt calendar = &m_calendar;
