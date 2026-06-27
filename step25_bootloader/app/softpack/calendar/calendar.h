/**
 * calendar.h — 时间管理模块接口
 *
 * 在 RTC 端口驱动之上提供:
 *   - 服务器时间同步 (dump_rtc_by_str)
 *   - 时间工具函数 (分钟转换 / 时间加减 / 重叠计算)
 *   - 分时电价时段系统 (slot_t / jfpg 解析 / 时段查找)
 *
 * 分层关系:
 *   sock_ctrl/message → calendar → drvp_rtc → SWM320_RTC → 硬件
 */

#ifndef __CALENDAR_H__
#define __CALENDAR_H__

#include <stdint.h>
#include "drvp_rtc.h"

/* ---- 最大时段数 ---- */
#define MAX_SLOTS  24
#define MAX_GROUPS  8

/* ---- 分时电价时段 ---- */
typedef struct {
    int group;       /* 费率组 (0 ~ MAX_GROUPS-1) */
    int start;       /* 开始分钟 (距 00:00) */
    int end;         /* 结束分钟 (距 00:00, 可跨天 >1440) */
} slot_t;

/* ---- 日历模块接口 ---- */
typedef struct {
    /* 初始化 RTC 硬件 (默认时间 2020-01-01 00:00:00) */
    void (*init)(void);

    /* 周期性工作: 从 RTC 刷新缓存时间 */
    void (*work)(void);

    /* 从服务器时间字符串同步 RTC
     * str: "YYYYMMDDhhmmss" (14 字符)
     * 返回 0 成功, -1 格式错误 */
    int  (*sync_by_str)(char *str);

    /* 获取当前缓存时间 */
    void (*get_dt)(RTC_DateTime *dt);

    /* 直接设置 RTC 时间 (stop→init→start) */
    void (*set_dt)(RTC_DateTime *dt);

    /* 将 HH:MM 转为当日分钟数 (0-1439) */
    int  (*to_minutes)(uint8_t hour, uint8_t min);

    /* 在时段列表中查找当前时间属于哪个时段
     * 返回时段索引 (0-based), -1 = 未找到 */
    int  (*get_time_segments)(slot_t *list, int len, int hour, int min);

    /* 计算两个时段的重叠分钟数 */
    int  (*calculate_overlap)(slot_t period0, slot_t period1);

    /* 将日期时间转为绝对分钟数 (从 0000-01-01 起算) */
    uint32_t (*datetime_to_minutes)(RTC_DateTime dt);

    /* 日期时间加指定分钟 */
    void (*add_minutes)(RTC_DateTime *dt, int minutes);

    /* 解析 jfpg 时段字符串并存储
     * 格式: "00:00-08:00" "08:00-10:00" ... (每个时段空格分隔)
     * 返回解析到的时段数 */
    int  (*parse_jfpg)(int argc, char **argv);

    /* 获取已存储的时段列表 */
    int  (*get_slots)(slot_t **slots);

} calendar_t, *calendar_pt;

extern calendar_pt calendar;

#endif /* __CALENDAR_H__ */
