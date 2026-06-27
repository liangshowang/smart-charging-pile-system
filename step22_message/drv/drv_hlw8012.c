/**
 * drv_hlw8012.c — HLW8012 计量芯片驱动实现
 *
 * 原理:
 *   HLW8012 将电压和电流采样信号相乘后累加, 达到内部固定阈值时输出一个
 *   下降沿脉冲。脉冲频率正比于瞬时功率:
 *     P(W) = (pulse / cycle_sec) × k + b
 *   其中 k, b 是校准系数 (后续从 FlashDB 读取, 当前阶段用默认值)。
 *
 * 工作流程:
 *   1. init(): 配置 GPIO 为输入, 绑定下降沿中断
 *   2. ISR: 下降沿 → pulse[road]++ → 记录 tick
 *   3. work(): task_listen 每 100ms 调用
 *      - 检测拔枪: 3 秒无脉冲 → 打印警告
 *      - 每 5 秒: 读取脉冲 → 计算瞬时功率 → sysprt 输出
 *      - 每 60 秒: 累计分钟脉冲 → 输出分钟电量
 */

#include "drv_hlw8012.h"
#include "drv_gpio.h"
#include "io_config.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- 引脚映射 (road → 物理引脚) ---- */
static const int pin_map[HLW_ROAD_NUM] = {
    PIN_HLW0,   /* road 0: C3(22) */
    PIN_HLW1,   /* road 1: C7(20) */
};

/* ---- 脉冲计数 (volatile: ISR 写入, 任务读取) ---- */
static volatile uint32_t pulse[HLW_ROAD_NUM];
static volatile uint32_t last_tick[HLW_ROAD_NUM];

/* ---- ISR 参数 (每路独立, 保存 road 号) ---- */
typedef struct {
    int road;
} isr_arg_t;

static isr_arg_t isr_arg[HLW_ROAD_NUM];

/* ================================================================
 * pulse_isr — GPIO 下降沿中断回调
 *
 * 在中断上下文中执行:
 *   1. 递增脉冲计数
 *   2. 记录最后一次脉冲的时间戳
 *
 * 使用 xTaskGetTickCountFromISR() 获取 ISR 安全的 tick。
 * 不做复杂处理——仅计数, 计算留给 work()。
 * ================================================================ */
static void pulse_isr(void *args)
{
    isr_arg_t *arg = (isr_arg_t *)args;
    int road = arg->road;

    if (road < 0 || road >= HLW_ROAD_NUM) return;

    pulse[road]++;
    last_tick[road] = xTaskGetTickCountFromISR();
}

/* ================================================================
 * do_init — 初始化 HLW8012 两路计量
 *
 * 依次配置每路:
 *   1. 设为输入模式 (内部上拉, 脉冲空闲为高电平)
 *   2. 绑定下降沿中断回调
 *   3. 使能中断
 * ================================================================ */
static void do_init(void)
{
    int road;

    for (road = 0; road < HLW_ROAD_NUM; road++) {
        int pin = pin_map[road];

        /* 清零计数器 */
        pulse[road] = 0;
        last_tick[road] = 0;

        /* ISR 参数 */
        isr_arg[road].road = road;

        /* 配置为输入 (内部上拉: 空闲=高, 脉冲=低) */
        drv_gpio->set_mode(pin, PIN_MODE_INPUT_PULLUP);

        /* 绑定下降沿中断 */
        drv_gpio->attach_irq(pin, PIN_IRQ_MODE_FALLING,
                             pulse_isr, &isr_arg[road]);

        /* 使能中断 */
        drv_gpio->irq_enable(pin, PIN_IRQ_ENABLE);
    }

    sysprt->alog("[hlw8012] init done, 2 roads configured\r\n");
}

/* ================================================================
 * do_work — 计量轮询 (task_listen 每 100ms 调用)
 *
 * 职责:
 *   1. 拔枪检测: 3 秒无脉冲 → 认为插座已拔出
 *   2. 周期上报: 每 5 秒输出脉冲数 (供上层计算瞬时功率)
 *   3. 分钟累计: 每 60 秒输出分钟脉冲 (供上层计算分钟电量)
 * ================================================================ */
static void do_work(void)
{
    static uint32_t otick_cyc = 0;         /* 上次周期上报的 tick */
    static uint32_t min_pulse[HLW_ROAD_NUM] = {0, 0};
    static uint32_t min_cntr = 0;
    uint32_t ntick = xTaskGetTickCount();
    int road;

    /* ---- 1. 拔枪检测 (3 秒无脉冲) ---- */
    for (road = 0; road < HLW_ROAD_NUM; road++) {
        if (last_tick[road] == 0) continue;  /* 尚未收到过脉冲 */

        if ((ntick - last_tick[road]) >= pdMS_TO_TICKS(3000)) {
            /* 3 秒内无脉冲: 可能拔枪或无负载 */
            sysprt->alog("[hlw8012] road %d: no pulse for 3s, "
                         "socket may be unplugged\r\n", road);
            /* 重置计时, 避免重复报警 */
            last_tick[road] = ntick;
        }
    }

    /* ---- 2. 每 5 秒周期上报 ---- */
    if ((ntick - otick_cyc) >= pdMS_TO_TICKS(5000)) {
        otick_cyc = ntick;

        for (road = 0; road < HLW_ROAD_NUM; road++) {
            uint32_t p = pulse[road];

            /* 清零脉冲计数 (非原子读-清零, ISR 可能在中间写,
             * 但 32-bit 操作在 Cortex-M4 上是原子的, 且丢失一两个
             * 脉冲对功率计算影响可忽略) */
            pulse[road] = 0;

            /* 累加到分钟统计 */
            min_pulse[road] += p;

            sysprt->alog("[hlw8012] road %d: %lu pulses/5s\r\n",
                         road, p);
        }
        min_cntr++;
    }

    /* ---- 3. 每 60 秒 (12 个 5 秒周期) 分钟上报 ---- */
    if (min_cntr >= 12) {
        for (road = 0; road < HLW_ROAD_NUM; road++) {
            sysprt->alog("[hlw8012] road %d: %lu pulses/min\r\n",
                         road, min_pulse[road]);
            min_pulse[road] = 0;
        }
        min_cntr = 0;
    }
}

/* ================================================================
 * do_read_pulse — 读取并清零脉冲计数
 *
 * 返回当前脉冲数后清零, 供上层 (充电状态机) 使用。
 * 注意: 不是原子操作, 中断中可能同时写入。
 * ================================================================ */
static uint32_t do_read_pulse(int road)
{
    uint32_t val;

    if (road < 0 || road >= HLW_ROAD_NUM) return 0;

    val = pulse[road];
    pulse[road] = 0;
    return val;
}

/* ================================================================
 * do_get_pulse — 只读脉冲计数 (不清零)
 * ================================================================ */
static uint32_t do_get_pulse(int road)
{
    if (road < 0 || road >= HLW_ROAD_NUM) return 0;
    return pulse[road];
}

/* ================================================================
 * do_get_last_tick — 获取最后一次脉冲的 tick
 *
 * 供上层判断是否有负载 (脉冲间隔反映功率大小)。
 * ================================================================ */
static uint32_t do_get_last_tick(int road)
{
    if (road < 0 || road >= HLW_ROAD_NUM) return 0;
    return last_tick[road];
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static drv_hlw8012_t m_drv_hlw8012 = {
    do_init,
    do_work,
    do_read_pulse,
    do_get_pulse,
    do_get_last_tick,
};

drv_hlw8012_pt drv_hlw8012 = &m_drv_hlw8012;
