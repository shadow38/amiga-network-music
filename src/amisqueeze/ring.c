/*
 * ring.c - ring buffer of the portable core (see ring.h)
 */
#include <stdlib.h>
#include <string.h>

#include "ring.h"

int ring_init(struct ring *r, uint32_t size)
{
    r->buf = malloc(size);
    if (r->buf == NULL)
        return 0;
    r->size = size;
    ring_clear(r);
    return 1;
}

void ring_free(struct ring *r)
{
    free(r->buf);
    r->buf = NULL;
    r->size = 0;
}

void ring_clear(struct ring *r)
{
    r->rpos = r->wpos = r->used = 0;
    r->total_in = r->total_out = 0;
}

uint32_t ring_used(const struct ring *r)
{
    return r->used;
}

uint32_t ring_space(const struct ring *r)
{
    return r->size - r->used;
}

uint32_t ring_write_space(struct ring *r, uint8_t **p)
{
    uint32_t n = r->size - r->wpos;

    if (n > ring_space(r))
        n = ring_space(r);
    *p = r->buf + r->wpos;
    return n;
}

void ring_commit(struct ring *r, uint32_t n)
{
    r->wpos += n;
    if (r->wpos >= r->size)
        r->wpos -= r->size;
    r->used += n;
    r->total_in += n;
}

uint32_t ring_read(struct ring *r, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    uint32_t done = 0;

    if (n > r->used)
        n = r->used;
    while (done < n) {
        uint32_t chunk = r->size - r->rpos;
        if (chunk > n - done)
            chunk = n - done;
        memcpy(d + done, r->buf + r->rpos, chunk);
        ring_consume(r, chunk);
        done += chunk;
    }
    return done;
}

void ring_consume(struct ring *r, uint32_t n)
{
    r->rpos += n;
    if (r->rpos >= r->size)
        r->rpos -= r->size;
    r->used -= n;
    r->total_out += n;
}
