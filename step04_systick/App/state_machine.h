/**
 * 继电器状态机模块
 *
 * 使用 SysTick 全局计数器驱动，无阻塞延时。
 *
 * 状态序列 (循环):
 *   0 INIT  → 1 ELC0_ON  → 2 WAIT_5s  → 3 ELC0_OFF
 *   → 4 WAIT_2s → 5 ELC1_ON → 6 WAIT_5s → 7 ELC1_OFF → 8 WAIT_2s
 *   → 回到 1
 */

#ifndef __STATE_MACHINE_H__
#define __STATE_MACHINE_H__

void sm_init(void);
void sm_work(void);

#endif
