/**
 * drv_wdt.h — 看门狗驱动接口
 *
 * 依赖: SWM320_wdt.h (SDK)
 *
 * 使用:
 *   wdt->init(5000);       // 5 秒超时
 *   wdt->feed();           // 喂狗 (主循环中定期调用)
 *   wdt->disable();        // 调试时禁用
 *
 * SWM320 WDT 特性:
 *   - 32 位递减计数器
 *   - 两种模式: WDT_MODE_RESET (超时复位) / WDT_MODE_INTERRUPT (超时中断)
 *   - 独立时钟源 (HRC 12MHz/32)
 */

#ifndef __DRV_WDT_H__
#define __DRV_WDT_H__

#include <stdint.h>

/* ---- 驱动接口 ---- */
typedef struct {
    /**
     * init — 初始化并启动看门狗
     * @param timeout_ms  超时时间 (毫秒)
     *                    实际间隔 = timeout_ms * (HRC/32) / 1000 个计数
     */
    void (*init)(uint32_t timeout_ms);

    /** feed — 喂狗, 复位计数器到初始值 */
    void (*feed)(void);

    /** disable — 禁用看门狗 (仅调试) */
    void (*disable)(void);

    /** get_value — 读取当前计数器值 (调试用) */
    uint32_t (*get_value)(void);
} drv_wdt_t, *drv_wdt_pt;

/* ---- 全局接口指针 ---- */
extern drv_wdt_pt wdt;

#endif
