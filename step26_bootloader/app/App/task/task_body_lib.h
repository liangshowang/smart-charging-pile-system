/**
 * task_body_lib.h — FreeRTOS 任务封装层
 *
 * 为每个任务提供统一的"三明治"定义模式:
 *   __task_body_start → 实例化 m_task_body → task_entry 实现
 *   → __task_body_end → __task_body_quote_to
 *
 * task_body_t: 任务元数据 + 控制接口
 *   - handle     — FreeRTOS 任务句柄
 *   - name       — 任务名称 (FreeRTOS 标识)
 *   - cn_name    — 中文名称 (调试用)
 *   - stack_size — 堆栈尺寸 (words)
 *   - priority   — FreeRTOS 优先级
 *   - create     — 任务创建入口 (内部调用 xTaskCreate)
 *   - del        — 删除任务
 *   - suspend    — 挂起任务
 *   - recovery   — 恢复任务
 *
 * 使用示例:
 *
 *   // ---- my_task.c ----
 *   #include "task_inc.h"
 *
 *   __task_body_start;
 *   static task_body_t m_task_body = {
 *       .name = "my_task", .cn_name = "我的任务",
 *       .stack_size = 1024, .priority = 3,
 *       .handle = &handle, .create = create,
 *   };
 *   static void task_entry(void *arg) { while(1) { ... } }
 *   __task_body_end;
 *   __task_body_quote_to(my_task);
 *
 *   // ---- 其他文件 ----
 *   __task_body_quote(my_task);   // 声明 extern
 *   my_task->create(NULL, NULL);  // 创建任务
 */

#ifndef __TASK_BODY_LIB_H__
#define __TASK_BODY_LIB_H__

#include "FreeRTOS.h"
#include "task.h"

/* ---- 类型别名 ---- */
#define task_handle_t TaskHandle_t

/* ---- task_body_t: 任务元数据 + 控制接口 ---- */
typedef struct {
    task_handle_t *handle;
    char          *name;       /* 任务名称 (FreeRTOS 标识) */
    char          *cn_name;    /* 中文名称 (调试用) */
    int            stack_size; /* 堆栈尺寸 (已废弃, 用 configMINIMAL_STACK_SIZE 单位) */
    int            priority;   /* FreeRTOS 优先级 */
    void (*create)(void *argc, void **argv);  /* 创建 */
    void (*del)(void);                        /* 删除 */
    void (*suspend)(void);                    /* 挂起 */
    void (*recovery)(void);                   /* 恢复 */
} task_body_t, *task_body_pt;

/* ================================================================
 * __task_body_quote_to(name) — 定义外部可见的任务指针
 *
 * 放在 .c 文件末尾, 使得其他模块可以通过 name->create() 等
 * 操作此任务。
 * ================================================================ */
#define __task_body_quote_to(name) \
    task_body_pt name = &m_task_body;

/* ================================================================
 * __task_body_quote(name) — 声明外部任务指针
 *
 * 放在 .h 文件 (如 task_inc.h) 中, 声明该任务指针存在。
 * ================================================================ */
#define __task_body_quote(name) \
    extern task_body_pt name;

/* ================================================================
 * __task_body_start — 任务文件头部声明
 *
 * 展开为:
 *   static task_handle_t handle;          — FreeRTOS 任务句柄
 *   static void task_entry(void *arg);    — 任务入口函数原型
 *   static void create(...);              — 创建函数原型
 * ================================================================ */
#define __task_body_start \
    static task_handle_t handle; \
    static void task_entry(void *arg); \
    static void create(void *argc, void **argv);

/* ================================================================
 * __task_body_end — 任务文件尾部定义
 *
 * 展开为:
 *   static task_body_pt task_body = &m_task_body;
 *   static void create(...) { xTaskCreate(...); }
 *
 * create() 从 m_task_body 读取 name/stack_size/priority/handle,
 * 调用 FreeRTOS xTaskCreate 创建任务。
 * ================================================================ */
#define __task_body_end \
    static task_body_pt task_body = &m_task_body; \
    static void create(void *argc, void **argv) \
    { \
        (void)argc; (void)argv; \
        xTaskCreate( \
            task_entry, \
            (const char *)task_body->name, \
            task_body->stack_size, \
            NULL, \
            task_body->priority, \
            task_body->handle \
        ); \
    }

#endif /* __TASK_BODY_LIB_H__ */
