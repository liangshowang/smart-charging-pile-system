/**
 * drv_loop.c — 环形缓冲驱动层实现
 *
 * 特性:
 *   - 1024 字节静态 buffer (在结构体内, 一次 malloc)
 *   - 留 1 个字节空位防止 head/rear 混淆
 *   - head 和 rear 单调递增, 用位与代替取模 (2 的幂大小)
 */

#include "drv_loop.h"
#include "FreeRTOS.h"        /* pvPortMalloc / vPortFree */

/* ================================================================
 * do_read — 从环形缓冲读 len 个字节
 *
 * 返回实际读到的字节数 (可能 < len, 如果缓冲数据不够)
 * ================================================================ */
static int do_read(loopbuf_pt ploop, void *vbuf, unsigned char len)
{
    int    i;
    char  *buf = (char *)vbuf;

    for (i = 0; i < len; i++) {
        if (ploop->rear == ploop->head)
            break;                          /* 缓冲空 */

        buf[i] = ploop->buffer[ploop->rear];
        ploop->rear = (ploop->rear + 1) & LOOP_MASK;
    }
    return i;
}

/* ================================================================
 * do_write — 向环形缓冲写 1 个字节
 *
 * 如果缓冲满, 静默丢弃 (ISR 环境不能阻塞)
 * ================================================================ */
static void do_write(loopbuf_pt ploop, uint8_t dat)
{
    /* 检查是否满: (head+1) % SIZE == rear */
    if (((ploop->head + 1) & LOOP_MASK) == ploop->rear)
        return;                             /* 满, 丢弃 */

    ploop->buffer[ploop->head] = dat;
    ploop->head = (ploop->head + 1) & LOOP_MASK;
}

/* ================================================================
 * do_reset — 清空环形缓冲
 * ================================================================ */
static void do_reset(loopbuf_pt ploop)
{
    ploop->head = 0;
    ploop->rear = 0;
}

/* ================================================================
 * do_create — 分配并初始化一个环形缓冲实例
 * ================================================================ */
static loopbuf_pt do_create(void)
{
    loopbuf_pt lb = (loopbuf_pt)pvPortMalloc(sizeof(loopbuf_t));
    if (lb) {
        lb->head = 0;
        lb->rear = 0;
    }
    return lb;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static loop_t m_loop = {
    do_read,
    do_write,
    do_reset,
    do_create,
};

loop_pt loop = &m_loop;
