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
uint8_t  stub_file[2 * 1024 * 1024];
int      stub_file_len;
int      stub_file_open;
int      stub_file_fails;
int      stub_file_limit;

struct stub_audio_req stub_audio[8];
int      stub_audio_fails;
int      stub_audio_unit;
int      stub_audio_open;
int32_t  stub_audio_abort_played;
int32_t  stub_audio_abort_real;
uint8_t  stub_audio_out[2 * 1024 * 1024];
int      stub_audio_out_len;
int      stub_audio_sends;
int      stub_audio_bad;
static int audio_fifo[64], audio_fifo_len;     /* queued requests, in playing order */

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

void stub_audio_clear(void)
{
    stub_audio_out_len = 0;
    stub_audio_sends = 0;
    stub_audio_bad = 0;
}

static void audio_capture(const uint8_t *d, uint32_t n)
{
    memcpy(stub_audio_out + stub_audio_out_len, d, n);
    stub_audio_out_len += (int)n;
}

static void audio_unqueue(int i)
{
    int k;

    for (k = 0; k < audio_fifo_len; k++)
        if (audio_fifo[k] == i) {
            memmove(audio_fifo + k, audio_fifo + k + 1, (size_t)(audio_fifo_len - k - 1) * sizeof(int));
            audio_fifo_len--;
            break;
        }
    stub_audio[i].queued = 0;
}

int stub_audio_finish(void)
{
    int i;

    if (audio_fifo_len == 0)
        return -1;
    i = audio_fifo[0];
    audio_capture(stub_audio[i].data, stub_audio[i].len);
    audio_unqueue(i);
    stub_audio[i].ended = 1;
    return i;
}

int stub_audio_queue(int *order)
{
    memcpy(order, audio_fifo, (size_t)audio_fifo_len * sizeof(int));
    return audio_fifo_len;
}

int plat_audio_open(int unit)
{
    stub_audio_unit = unit;
    if (stub_audio_fails) {
        msg("amisqueeze: cannot open ahi.device unit %d\n", unit);
        return 0;
    }
    memset(stub_audio, 0, sizeof(stub_audio));
    audio_fifo_len = 0;
    stub_audio_open = 1;
    return 1;
}

void plat_audio_close(void)
{
    stub_audio_open = 0;
}

void plat_audio_send(int i, const void *data, uint32_t len, uint32_t rate, int channels, int link)
{
    /* ahi.device plays a linked request only if the one it is linked to is playing (the first queued) */
    if (link >= 0 ? audio_fifo_len != 1 || audio_fifo[0] != link : audio_fifo_len != 0)
        stub_audio_bad++;
    stub_audio[i].data = data;
    stub_audio[i].len = len;
    stub_audio[i].rate = rate;
    stub_audio[i].channels = channels;
    stub_audio[i].link = link;
    stub_audio[i].queued = 1;
    stub_audio[i].ended = 0;
    audio_fifo[audio_fifo_len++] = i;
    stub_audio_sends++;
}

int plat_audio_done(int i)
{
    if (!stub_audio[i].ended)
        return 0;
    stub_audio[i].ended = 0;
    return 1;
}

/* The playing request (first queued) has played stub_audio_abort_played bytes; the others nothing */
int32_t plat_audio_abort(int i)
{
    int32_t played = 0;

    if (stub_audio[i].ended) {              /* ended before the abort: all of it */
        stub_audio[i].ended = 0;
        return (int32_t)stub_audio[i].len;
    }
    if (!stub_audio[i].queued)              /* already aborted with the one it was linked to */
        return 0;
    if (audio_fifo[0] != i)                 /* ahi.device: abort the one playing first */
        stub_audio_bad++;
    played = stub_audio_abort_played;
    if (played > 0)
        audio_capture(stub_audio[i].data, (uint32_t)played);
    else if (played < 0 && stub_audio_abort_real > 0)
        audio_capture(stub_audio[i].data, (uint32_t)stub_audio_abort_real);
    audio_unqueue(i);
    while (audio_fifo_len > 0)              /* the request linked to it goes with it */
        audio_unqueue(audio_fifo[0]);
    return played;
}

int plat_file_create(const char *name)
{
    (void)name;
    if (stub_file_fails)
        return -1;
    stub_file_len = 0;
    stub_file_open = 1;
    return STUB_FILE;
}

int plat_file_write(int file, const void *buf, int len)
{
    int max = stub_file_limit ? stub_file_limit : (int)sizeof(stub_file);

    if (file != STUB_FILE || !stub_file_open)
        return -1;
    if (len > max - stub_file_len)
        len = max - stub_file_len;
    memcpy(stub_file + stub_file_len, buf, (size_t)len);
    stub_file_len += len;
    return len;
}

void plat_file_close(int file)
{
    if (file == STUB_FILE)
        stub_file_open = 0;
}

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    (void)fds;
    (void)n;
    (void)timeout_ms;
    return -1;
}
