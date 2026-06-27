/**
 * drv_hlw8012.h — HLW8012 计量芯片驱动接口
 *
 * HLW8012 将电压/电流采样值相乘并累加, 达到固定阈值时输出一个脉冲。
 * MCU 通过 GPIO 中断 (下降沿) 计数脉冲, 周期性地读取并清零计数器,
 * 从而得到:
 *   - 瞬时功率: pulse/cycle 内脉冲数 → W
 *   - 累计电量: 总脉冲数 × 每脉冲能量 → kWh
 *
 * 两路独立计量 (2 个插座各一片 HLW8012):
 *   road 0: PIN_HLW0 (C3=22)
 *   road 1: PIN_HLW1 (C7=20)
 *
 * 接口指针模式:
 *   drv_hlw8012 → m_drv_hlw8012 → { init, work, read_pulse, get_pulse }
 */

#ifndef __DRV_HLW8012_H__
#define __DRV_HLW8012_H__

#include <stdint.h>

/* ---- 计量路数 ---- */
#define HLW_ROAD_NUM  2

/* ---- 操作接口 ---- */
typedef struct {
    void     (*init)(void);                            /* 初始化: 配置 GPIO + 中断 */
    void     (*work)(void);                            /* 周期轮询: 检测拔枪 + 上报脉冲 */
    uint32_t (*read_pulse)(int road);                  /* 读取并清零脉冲计数 */
    uint32_t (*get_pulse)(int road);                   /* 只读脉冲计数 (不清零) */
    uint32_t (*get_last_tick)(int road);               /* 最后一次脉冲的 tick */
} drv_hlw8012_t, *drv_hlw8012_pt;

/* ---- 全局接口指针 ---- */
extern drv_hlw8012_pt drv_hlw8012;

#endif /* __DRV_HLW8012_H__ */
