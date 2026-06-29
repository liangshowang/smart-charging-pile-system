/**
 * drv_adc.h — ADC 电压监测驱动接口
 *
 * 通过 ADC 采集板上电压 (如输入电源、分压后监测)。
 * 12-bit ADC, 可配置参考电压和分压比。
 *
 * 使用:
 *   adc->init(ADC_CH0);                // 初始化通道0
 *   uint16_t raw = adc->read_raw(0);   // 读原始值 (0~4095)
 *   uint32_t mv  = adc->read_mv(0);    // 读电压 (mV)
 *
 * 电压换算:
 *   V_actual = V_ref * raw / 4096 * divider
 *   例如: V_ref=3.3V, divider=5 (分压 5:1), raw=2048
 *     → V_actual = 3.3 * 2048 / 4096 * 5 = 8.25V
 */

#ifndef __DRV_ADC_H__
#define __DRV_ADC_H__

#include <stdint.h>

/* ---- 驱动接口 ---- */
typedef struct {
    /**
     * init — 初始化 ADC
     * @param channels  通道位掩码 (ADC_CH0 | ADC_CH1 | ...)
     *                  同时使能多个通道, 轮流读取
     */
    void (*init)(uint8_t channels);

    /**
     * read_raw — 读 ADC 原始值
     * @param chn  通道号 (0~7)
     * @return     12-bit ADC 值 (0~4095)
     */
    uint16_t (*read_raw)(uint8_t chn);

    /**
     * read_mv — 读 ADC 并换算为电压 (mV)
     * @param chn       通道号 (0~7)
     * @param vref_mv   参考电压 (mV, 典型 3300)
     * @param divider   外部分压比 (分压电阻 R1/(R1+R2) 的倒数)
     *                  divider=1 表示无分压
     * @return          实际电压 (mV)
     */
    uint32_t (*read_mv)(uint8_t chn, uint32_t vref_mv, uint32_t divider);
} drv_adc_t, *drv_adc_pt;

/* ---- 全局接口指针 ---- */
extern drv_adc_pt adc;

#endif
