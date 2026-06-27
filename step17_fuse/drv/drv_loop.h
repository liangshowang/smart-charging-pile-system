/**
 * drv_loop.h — 环形缓冲驱动层接口
 *
 * 用接口指针模式封装环形缓冲, 支持创建多个实例。
 * 缓冲大小: 1024 字节, 必须是 2 的幂 (便于用位与代替取模)。
 */

#ifndef __DRV_LOOP_H__
#define __DRV_LOOP_H__

#include <stdint.h>

/* ---- 缓冲大小 (必须是 2 的幂) ---- */
#define LOOP_BUF_SIZE  1024
#define LOOP_MASK      (LOOP_BUF_SIZE - 1)

/* ---- 环形缓冲结构体 ---- */
typedef struct {
    int      head;                    /* 写指针 */
    int      rear;                    /* 读指针 */
    uint8_t  buffer[LOOP_BUF_SIZE];   /* 数据区 */
} loopbuf_t, *loopbuf_pt;

/* ---- 驱动接口 (虚函数表) ---- */
typedef struct {
    int         (*read)(loopbuf_pt ploop, void *buf, unsigned char len);
    void        (*write)(loopbuf_pt ploop, uint8_t dat);
    void        (*reset)(loopbuf_pt ploop);
    loopbuf_pt  (*create)(void);
} loop_t, *loop_pt;

/* ---- 全局接口指针 ---- */
extern loop_pt loop;

#endif /* __DRV_LOOP_H__ */
