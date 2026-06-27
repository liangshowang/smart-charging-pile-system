/**
 * drv_fuse.h — 保险丝检测驱动接口
 *
 * 原理:
 *   220V 50Hz 交流电经过光耦产生 50 个脉冲/秒。
 *   MCU 轮询 GPIO 读取脉冲, 统计每秒脉冲数。
 *   脉冲 < 10/sec → 保险丝熔断 (光耦失去供电, 停止脉冲)。
 *
 * 接口指针模式:
 *   drv_fuse → m_drv_fuse → { init, work, is_err }
 */

#ifndef __DRV_FUSE_H__
#define __DRV_FUSE_H__

#include <stdint.h>

/* ---- 操作接口 ---- */
typedef struct {
    void (*init)  (void);         /* 初始化: 配置 GPIO 为输入 */
    void (*work)  (void);         /* 轮询检测: 每 5ms 调用, 计数脉冲 + 1s 判定 */
    int  (*is_err)(void);         /* 返回保险丝状态: 0=正常, 1=熔断 */
} drv_fuse_t, *drv_fuse_pt;

/* ---- 全局接口指针 ---- */
extern drv_fuse_pt drv_fuse;

#endif /* __DRV_FUSE_H__ */
