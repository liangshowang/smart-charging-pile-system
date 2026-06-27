/**
 * 环形缓冲区 (Ring Buffer)
 *
 * 静态内存分配，无 malloc。ISR 安全——只由单生产者(ISR)写、单消费者(main)读，
 * 所以不需要关中断保护。
 *
 * head: 写指针 (ISR 推进)
 * tail: 读指针 (main 推进)
 * 空: head == tail
 * 满: (head + 1) % size == tail  (保留 1 字节防止头尾重叠)
 */

#ifndef __LOOPBUF_H__
#define __LOOPBUF_H__

#include <stdint.h>

#define LBUF_SIZE  256

typedef struct {
    uint8_t  buffer[LBUF_SIZE];
    uint32_t head;      /* 写位置 */
    uint32_t tail;      /* 读位置 */
} loopbuf_t;

void     loopbuf_init(loopbuf_t *lb);
uint32_t loopbuf_write(loopbuf_t *lb, uint8_t byte);
uint32_t loopbuf_read(loopbuf_t *lb, uint8_t *byte);
uint32_t loopbuf_available(loopbuf_t *lb);

#endif
