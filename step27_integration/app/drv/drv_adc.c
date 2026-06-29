/**
 * drv_adc.c — ADC 驱动实现
 *
 * SWM320 ADC 特性:
 *   - 12-bit SAR ADC
 *   - 8 个输入通道 (CH0~CH7)
 *   - 软件触发模式
 *   - 可配置采样平均 (1/2/4/8/16 次)
 *   - 时钟源: HRC (12MHz) 或 VCO 分频
 *
 * 使用建议:
 *   - 充电桩可监测 220V 经变压器降压后的直流电压
 *   - 分压电阻网络: R_top=100k, R_bottom=10k → divider≈11
 *   - 实际电压 = read_mv(chn, 3300, 11)
 */

#include "SWM320.h"
#include "SWM320_adc.h"
#include "drv_adc.h"

/* ---- 内部状态 ---- */
static uint8_t  g_channels = 0;
static uint8_t  g_inited   = 0;

/* ---- 前向声明 ---- */
static void     do_init(uint8_t channels);
static uint16_t do_read_raw(uint8_t chn);
static uint32_t do_read_mv(uint8_t chn, uint32_t vref_mv, uint32_t divider);

/* ================================================================
 * do_init — 初始化 ADC
 *
 * 配置:
 *   - 时钟: VCO/64 ≈ 1.875MHz (稳定)
 *   - 采样平均: 8 次 (抗干扰)
 *   - 触发: 软件触发 (SW)
 *   - 模式: 单次转换
 * ================================================================ */
static void do_init(uint8_t channels)
{
    ADC_InitStructure ais;

    g_channels = channels;

    /* 配置 ADC */
    ais.clk_src   = ADC_CLKSRC_VCO_DIV64;  /* 慢时钟, 稳定 */
    ais.clk_div   = 1;                     /* 不分频 */
    ais.pga_ref   = PGA_REF_INTERNAL;      /* 内部参考 */
    ais.channels  = channels;              /* 使能的通道 */
    ais.samplAvg  = ADC_AVG_SAMPLE8;       /* 8 次平均 */
    ais.trig_src  = ADC_TRIGSRC_SW;        /* 软件触发 */
    ais.Continue  = 0;                     /* 单次转换 */
    ais.EOC_IEn   = 0;                     /* 不使能中断 */
    ais.OVF_IEn   = 0;
    ais.HFULL_IEn = 0;
    ais.FULL_IEn  = 0;

    ADC_Init(ADC0, &ais);
    ADC_Open(ADC0);

    g_inited = 1;
}

/* ================================================================
 * do_read_raw — 读 ADC 原始值 (阻塞转换)
 * ================================================================ */
static uint16_t do_read_raw(uint8_t chn)
{
    uint32_t chn_mask;

    if (!g_inited) return 0;
    if (chn > 7) return 0;

    chn_mask = (1u << chn);

    /* 选择通道并启动转换 */
    ADC_ChnSelect(ADC0, chn_mask);
    ADC_Start(ADC0);

    /* 等待转换完成 */
    while (!ADC_IsEOC(ADC0, chn_mask)) {
        __NOP();
    }

    return (uint16_t)ADC_Read(ADC0, chn_mask);
}

/* ================================================================
 * do_read_mv — 读 ADC 并换算为电压 (mV)
 *
 * 公式: V = (raw / 4096) * vref * divider
 * 为避免浮点, 先乘后除:
 *   V_mV = (raw * vref_mv * divider) / 4096
 * 用 uint64 防溢出 (raw最大4095, vref最大3300, divider最大100)
 *   → 中间值最大 4095*3300*100 = 1,351,350,000 < 2^31 (安全)
 * ================================================================ */
static uint32_t do_read_mv(uint8_t chn, uint32_t vref_mv, uint32_t divider)
{
    uint16_t raw;
    uint64_t mv;

    raw = do_read_raw(chn);
    if (raw == 0) return 0;

    mv = (uint64_t)raw * vref_mv * divider;
    mv /= 4096;

    return (uint32_t)mv;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_adc_t m_adc = {
    do_init,
    do_read_raw,
    do_read_mv,
};

drv_adc_pt adc = &m_adc;
