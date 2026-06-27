/**
 * task_inc.h — 任务统一入口
 *
 * 所有模块通过此头文件访问任务:
 *   #include "task_inc.h"
 *   task_host->create(NULL, NULL);
 *   task_start->suspend();
 *
 * 四个任务:
 *   task_start  — 初始化硬件 + 创建其他任务 + 主调度循环
 *   task_host   — LED 刷新 + 命令行处理
 *   task_ctrl   — 插座控制 + OTA 处理 (占位)
 *   task_listen — HLW8012 快速轮询 (占位)
 */

#ifndef __TASK_INC_H__
#define __TASK_INC_H__

#include "task_body_lib.h"

/* ---- 四个任务指针声明 ---- */
__task_body_quote(task_start);
__task_body_quote(task_host);
__task_body_quote(task_ctrl);
__task_body_quote(task_listen);

#endif /* __TASK_INC_H__ */
