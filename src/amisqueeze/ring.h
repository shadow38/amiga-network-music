/*
 * ring.h - ring buffer of the portable core: the stream data waiting to be played.
 *
 * Bytes are written in place (ring_write_space, then ring_commit) and consumed from the read side (ring_read or
 * ring_consume). total_in and total_out count every byte that went through since the last ring_clear(): they
 * mark positions in the stream (track boundaries) whatever the wrap-around.
 */
#ifndef RING_H
#define RING_H

#include <stdint.h>

struct ring {
    uint8_t *buf;
    uint32_t size;
    uint32_t rpos, wpos;                /* read and write offsets in buf */
    uint32_t used;
    uint64_t total_in, total_out;
};

/* Allocates the buffer; 0 if there is not enough memory */
int ring_init(struct ring *r, uint32_t size);
void ring_free(struct ring *r);
/* Empties the buffer and resets the totals */
void ring_clear(struct ring *r);

uint32_t ring_used(const struct ring *r);
uint32_t ring_space(const struct ring *r);

/* Contiguous free space at the write position: returns its size and sets *p to it */
uint32_t ring_write_space(struct ring *r, uint8_t **p);
/* Accounts for n bytes written there (n <= what ring_write_space returned) */
void ring_commit(struct ring *r, uint32_t n);

/* Contiguous data at the read position: returns its size and sets *p to it (consume it with ring_consume) */
uint32_t ring_read_space(const struct ring *r, const uint8_t **p);
/* Copies up to n bytes out of the buffer; returns the number copied */
uint32_t ring_read(struct ring *r, void *dst, uint32_t n);
/* Drops n bytes from the read side (n <= ring_used) */
void ring_consume(struct ring *r, uint32_t n);

#endif
