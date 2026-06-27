#include "loopbuf.h"

void loopbuf_init(loopbuf_t *lb)
{
    lb->head = 0;
    lb->tail = 0;
}

uint32_t loopbuf_write(loopbuf_t *lb, uint8_t byte)
{
    uint32_t next = (lb->head + 1) % LBUF_SIZE;

    if (next == lb->tail)   /* 满，丢弃 */
        return 0;

    lb->buffer[lb->head] = byte;
    lb->head = next;
    return 1;
}

uint32_t loopbuf_read(loopbuf_t *lb, uint8_t *byte)
{
    if (lb->head == lb->tail)   /* 空 */
        return 0;

    *byte = lb->buffer[lb->tail];
    lb->tail = (lb->tail + 1) % LBUF_SIZE;
    return 1;
}

uint32_t loopbuf_available(loopbuf_t *lb)
{
    if (lb->head >= lb->tail)
        return lb->head - lb->tail;
    else
        return LBUF_SIZE - (lb->tail - lb->head);
}
