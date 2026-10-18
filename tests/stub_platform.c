/*
 * stub_platform.c - fake implementation of platform.h for the unit tests (see stub_platform.h)
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "stub_platform.h"

uint8_t  stub_tx[8192];
int      stub_tx_len;
uint8_t  stub_stream_tx[2048];
int      stub_stream_tx_len;
char     stub_out[16384];
uint32_t stub_now;
int      stub_chunk;
int      stub_connect_fails;
int      stub_stream_open;

struct queue {
    uint8_t data[2 * 1024 * 1024];
    int     len, pos;
};

static struct queue rx;                 /* control connection */
static struct queue srx;                /* stream connection */
static int stream_ended;

void stub_reset(void)
{
    stub_tx_len = 0;
    stub_stream_tx_len = 0;
    stub_out[0] = '\0';
    rx.len = rx.pos = 0;
    srx.len = srx.pos = 0;
    stream_ended = 0;
    stub_stream_open = 0;
}

static void put(struct queue *q, const void *data, int len)
{
    memcpy(q->data + q->len, data, (size_t)len);
    q->len += len;
}

void stub_feed(const void *data, int len)
{
    put(&rx, data, len);
}

void stub_feed_packet(const char *opcode, const void *body, int len)
{
    uint8_t head[6];
    int n = 4 + len;

    head[0] = (uint8_t)(n >> 8);
    head[1] = (uint8_t)n;
    memcpy(head + 2, opcode, 4);
    stub_feed(head, 6);
    stub_feed(body, len);
}

int stub_pending(void)
{
    return rx.len - rx.pos;
}

void stub_feed_stream(const void *data, int len)
{
    put(&srx, data, len);
}

void stub_stream_end(void)
{
    stream_ended = 1;
}

int stub_stream_pending(void)
{
    return srx.len - srx.pos;
}

void msg(const char *fmt, ...)
{
    size_t used = strlen(stub_out);
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(stub_out + used, sizeof(stub_out) - used, fmt, ap);
    va_end(ap);
}

void msg_raw(const void *buf, int len)
{
    size_t used = strlen(stub_out);

    if (used + (size_t)len < sizeof(stub_out)) {
        memcpy(stub_out + used, buf, (size_t)len);
        stub_out[used + (size_t)len] = '\0';
    }
}

uint32_t plat_now_ms(void)
{
    return stub_now;
}

int plat_resolve(const char *host, uint32_t *ip)
{
    (void)host;
    *ip = 0x7f000001;
    return 1;
}

int plat_connect(uint32_t ip, uint16_t port)
{
    (void)ip;
    (void)port;
    return STUB_CONTROL;
}

int plat_connect_start(uint32_t ip, uint16_t port)
{
    (void)ip;
    (void)port;
    stub_stream_open = 1;
    return STUB_STREAM;
}

int plat_connect_finish(int sock)
{
    (void)sock;
    return !stub_connect_fails;
}

int plat_send(int sock, const void *buf, int len)
{
    if (sock == STUB_STREAM) {
        memcpy(stub_stream_tx + stub_stream_tx_len, buf, (size_t)len);
        stub_stream_tx_len += len;
    } else {
        memcpy(stub_tx + stub_tx_len, buf, (size_t)len);
        stub_tx_len += len;
    }
    return len;
}

int plat_recv(int sock, void *buf, int len)
{
    struct queue *q = sock == STUB_STREAM ? &srx : &rx;
    int n = q->len - q->pos;

    if (n > len)
        n = len;
    if (stub_chunk > 0 && n > stub_chunk)
        n = stub_chunk;
    if (n == 0 && sock == STUB_STREAM && !stream_ended)
        return -1;                      /* nothing yet: the tests only read when data is queued */
    memcpy(buf, q->data + q->pos, (size_t)n);
    q->pos += n;
    return n;                           /* 0 when nothing is left = connection closed */
}

void plat_close(int sock)
{
    if (sock == STUB_STREAM)
        stub_stream_open = 0;
}

int plat_error(void)
{
    return 0;
}

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    (void)fds;
    (void)n;
    (void)timeout_ms;
    return -1;
}
