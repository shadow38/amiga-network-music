/*
 * test_ring.c - unit tests of ring.c (host build, see "make test"): the ring buffer
 */
#include "test_core.h"

static void test_ring(void)
{
    struct ring r;
    uint8_t *w, out[16];
    const uint8_t *rp;
    uint32_t n;

    CHECK(ring_init(&r, 16));
    n = ring_write_space(&r, &w);
    CHECK(n == 16);
    memcpy(w, "0123456789", 10);
    ring_commit(&r, 10);
    ring_consume(&r, 6);                /* "6789" left, write position 10 */
    n = ring_write_space(&r, &w);
    CHECK(n == 6);                      /* contiguous up to the end */
    memcpy(w, "abcdef", 6);
    ring_commit(&r, 6);
    n = ring_write_space(&r, &w);
    CHECK(n == 6 && w == r.buf);        /* wrapped around: free up to the read position (6) */
    memcpy(w, "gh", 2);
    ring_commit(&r, 2);
    CHECK(ring_used(&r) == 12 && ring_space(&r) == 4);
    n = ring_read_space(&r, &rp);
    CHECK(n == 10 && rp == r.buf + 6);  /* contiguous data up to the end, the rest is at the start */
    CHECK(ring_peek(&r, 8, out, 4) == 4 && !memcmp(out, "efgh", 4));      /* across the end */
    CHECK(ring_peek(&r, 10, out, 4) == 2 && !memcmp(out, "gh", 2));       /* from the start of the buffer */
    CHECK(ring_peek(&r, 11, out, 4) == 1 && out[0] == 'h');               /* from past the start */
    CHECK(ring_peek(&r, 0, out, 3) == 3 && !memcmp(out, "678", 3));
    CHECK(ring_peek(&r, 12, out, 4) == 0 && ring_used(&r) == 12 && r.total_out == 6);
    CHECK(ring_read(&r, out, 16) == 12 && !memcmp(out, "6789abcdefgh", 12));
    CHECK(r.total_in == 18 && r.total_out == 18 && ring_used(&r) == 0);
    ring_clear(&r);
    CHECK(r.total_in == 0 && r.total_out == 0);
    ring_free(&r);
}

/* Dropping the last bytes written (strm f): across the end of the buffer, never what was consumed */
static void test_ring_truncate(void)
{
    struct ring r;
    uint8_t *w, out[16];

    CHECK(ring_init(&r, 16));
    ring_write_space(&r, &w);
    memcpy(w, "0123456789abcdef", 16);
    ring_commit(&r, 16);
    ring_consume(&r, 6);
    ring_write_space(&r, &w);
    memcpy(w, "gh", 2);                 /* "6789abcdefgh", the write position wrapped around to 2 */
    ring_commit(&r, 2);
    ring_truncate(&r, 20);              /* past what was written: nothing to drop */
    CHECK(ring_used(&r) == 12 && r.total_in == 18);
    ring_truncate(&r, 14);              /* back across the end of the buffer: "6789abcd" left */
    CHECK(ring_used(&r) == 8 && r.total_in == 14 && r.wpos == 14);
    CHECK(ring_write_space(&r, &w) == 2 && w == r.buf + 14);
    memcpy(w, "XY", 2);
    ring_commit(&r, 2);
    CHECK(ring_peek(&r, 0, out, 16) == 10 && !memcmp(out, "6789abcdXY", 10));
    ring_consume(&r, 4);
    ring_truncate(&r, 8);               /* below what was consumed (10): everything left is dropped */
    CHECK(ring_used(&r) == 0 && r.total_in == 10 && r.total_out == 10 && r.wpos == r.rpos);
    ring_write_space(&r, &w);
    memcpy(w, "ij", 2);
    ring_commit(&r, 2);
    CHECK(ring_read(&r, out, 16) == 2 && !memcmp(out, "ij", 2) && r.total_out == 12);
    ring_free(&r);
}

void tests_ring(void)
{
    test_ring();
    test_ring_truncate();
}
