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
int      stub_wait[8][2];
int      stub_waits;
int      stub_wait_advances;
int      stub_waits_without_control;
int      stub_again;
int      stub_connect_interrupted;
int      stub_resolve_fails;
int      stub_udp_replies;
int      stub_send_fail_in;
int      stub_send_interrupted;
int      stub_recv_interrupted;
static int interrupted;
static int udp_given;                   /* discovery replies received */

struct stub_audio_req stub_audio[8];
int      stub_audio_fails;
int      stub_audio_unit;
int      stub_priority;
int      stub_audio_mode_known;
struct audio_mode stub_audio_mode;
int      stub_audio_open;
int32_t  stub_audio_abort_played;
int32_t  stub_audio_abort_real;
uint8_t  stub_audio_out[2 * 1024 * 1024];
int      stub_audio_out_len;
int      stub_audio_sends;
int      stub_audio_bad;
static int audio_fifo[64], audio_fifo_len;     /* queued requests, in playing order */

struct stub_paula_req stub_paula[8];
int      stub_paula_fails;
int      stub_paula_open;
uint32_t stub_paula_clock = 3546895;
int32_t  stub_paula_abort_played;
uint8_t  stub_paula_out[4][STUB_PAULA_OUT];
uint32_t stub_paula_out_len;
int      stub_paula_high, stub_paula_low;
int      stub_paula_volumes;
int      stub_paula_sends;
int      stub_paula_bad;
static uint8_t paula_buf[8][4][PAULA_BLOCK_SAMPLES];
static int paula_fifo[8], paula_fifo_len;      /* queued blocks, in playing order */

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
    stub_waits = 0;
    stub_wait_advances = 0;
    stub_waits_without_control = 0;
    stub_again = 0;
    stub_connect_interrupted = 0;
    stub_resolve_fails = 0;
    stub_udp_replies = 0;
    udp_given = 0;
    stub_send_fail_in = 0;
    stub_send_interrupted = 0;
    stub_recv_interrupted = 0;
    interrupted = 0;
    stub_priority = STUB_PRI_NONE;
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

void plat_set_priority(int pri)
{
    stub_priority = pri;
}

int plat_resolve(const char *host, uint32_t *ip)
{
    (void)host;
    if (stub_resolve_fails)
        return 0;
    *ip = 0x7f000001;
    return 1;
}

int plat_connect(uint32_t ip, uint16_t port)
{
    (void)ip;
    (void)port;
    if (stub_connect_interrupted) {
        interrupted = 1;
        return -2;
    }
    return STUB_CONTROL;
}

int plat_interrupted(void)
{
    return interrupted;
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
        if (stub_send_fail_in > 0 && --stub_send_fail_in == 0) {
            if (stub_send_interrupted)
                interrupted = 1;
            return -1;
        }
        memcpy(stub_tx + stub_tx_len, buf, (size_t)len);
        stub_tx_len += len;
    }
    return len;
}

int plat_recv(int sock, void *buf, int len)
{
    struct queue *q = sock == STUB_STREAM ? &srx : &rx;
    int n = q->len - q->pos;

    if (sock != STUB_STREAM && stub_recv_interrupted) {
        stub_recv_interrupted = 0;
        interrupted = 1;
        return -1;
    }
    if (sock != STUB_STREAM && stub_again > 0) {
        stub_again--;
        return PLAT_AGAIN;
    }
    if (n > len)
        n = len;
    if (stub_chunk > 0 && n > stub_chunk)
        n = stub_chunk;
    if (n == 0 && sock == STUB_STREAM && !stream_ended)
        return PLAT_AGAIN;              /* nothing yet */
    memcpy(buf, q->data + q->pos, (size_t)n);
    q->pos += n;
    return n;                           /* 0 when nothing is left = connection closed */
}

/* Server discovery: tried against the fake server (tests/fake_server.py), here only with stub_udp_replies */
int plat_udp_open(void)
{
    return stub_udp_replies > 0 ? STUB_UDP : -1;
}

int plat_udp_broadcast(int sock, uint16_t port, const void *buf, int len)
{
    (void)sock; (void)port; (void)buf; (void)len;
    return 1;
}

int plat_udp_recv(int sock, void *buf, int len, uint32_t *ip)
{
    (void)sock;
    if (udp_given >= stub_udp_replies || len < 7)
        return -1;
    memcpy(buf, "ENAME\001", 6);
    ((uint8_t *)buf)[6] = (uint8_t)('A' + udp_given);
    *ip = 0x7f000002 + (uint32_t)udp_given++;
    return 7;
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

int plat_audio_mode(int unit, struct audio_mode *m)
{
    (void)unit;
    if (!stub_audio_mode_known)
        return 0;
    *m = stub_audio_mode;
    return 1;
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

void stub_paula_clear(void)
{
    stub_paula_out_len = 0;
    stub_paula_volumes = 0;
    stub_paula_sends = 0;
    stub_paula_bad = 0;
}

static void paula_capture(int b, uint32_t n)
{
    int c;

    for (c = 0; c < stub_paula_open; c++)
        memcpy(stub_paula_out[c] + stub_paula_out_len, paula_buf[b][c] + stub_paula[b].start, n);
    stub_paula_out_len += n;
}

static void paula_unqueue(int b)
{
    int k;

    for (k = 0; k < paula_fifo_len; k++)
        if (paula_fifo[k] == b) {
            memmove(paula_fifo + k, paula_fifo + k + 1, (size_t)(paula_fifo_len - k - 1) * sizeof(int));
            paula_fifo_len--;
            break;
        }
    stub_paula[b].queued = 0;
}

int stub_paula_finish(void)
{
    int b;

    if (paula_fifo_len == 0)
        return -1;
    b = paula_fifo[0];
    paula_capture(b, stub_paula[b].len);
    paula_unqueue(b);
    stub_paula[b].ended = 1;
    if (paula_fifo_len > 0)
        stub_paula[paula_fifo[0]].playing = 1;
    return b;
}

int stub_paula_queue(int *order)
{
    memcpy(order, paula_fifo, (size_t)paula_fifo_len * sizeof(int));
    return paula_fifo_len;
}

int plat_paula_open(int channels)
{
    if (stub_paula_fails) {
        msg("amisqueeze: cannot open audio.device\n");
        return 0;
    }
    if (channels != 4 && channels != 2)
        stub_paula_bad++;
    memset(stub_paula, 0, sizeof(stub_paula));
    memset(paula_buf, 0, sizeof(paula_buf));
    paula_fifo_len = 0;
    stub_paula_open = channels;
    return 1;
}

void plat_paula_close(void)
{
    stub_paula_open = 0;
}

uint32_t plat_paula_clock(void)
{
    return stub_paula_clock;
}

uint8_t *plat_paula_buffer(int b, int c)
{
    if (c >= stub_paula_open)
        stub_paula_bad++;
    return paula_buf[b][c];
}

void plat_paula_send(int b, uint32_t start, uint32_t len, uint32_t period)
{
    if (!stub_paula_open || (start | len) & 1 || len == 0 || start + len > PAULA_BLOCK_SAMPLES ||
        period < PAULA_PERIOD_MIN || period > PAULA_PERIOD_MAX || paula_fifo_len >= 2)
        stub_paula_bad++;
    stub_paula[b].start = start;
    stub_paula[b].len = len;
    stub_paula[b].period = period;
    stub_paula[b].queued = 1;
    stub_paula[b].ended = 0;
    stub_paula[b].playing = paula_fifo_len == 0;
    paula_fifo[paula_fifo_len++] = b;
    stub_paula_sends++;
}

int plat_paula_done(int b)
{
    if (!stub_paula[b].ended)
        return 0;
    stub_paula[b].ended = 0;
    return 1;
}

/* The block playing has played stub_paula_abort_played samples; one queued behind it, nothing (aborted at once) */
int32_t plat_paula_abort(int b)
{
    int32_t played = 0;

    if (stub_paula[b].ended) {              /* ended before the abort: all of it */
        stub_paula[b].ended = 0;
        return (int32_t)stub_paula[b].len;
    }
    if (!stub_paula[b].queued)
        return 0;
    if (paula_fifo[0] != b)
        stub_paula_bad++;
    if (stub_paula[b].playing) {
        played = stub_paula_abort_played;
        if (played > 0)
            paula_capture(b, (uint32_t)played);
    }
    paula_unqueue(b);
    return played;
}

void plat_paula_volume(int high, int low)
{
    if (high < 0 || high > 64 || low < 0 || low > 64)
        stub_paula_bad++;
    stub_paula_high = high;
    stub_paula_low = low;
    stub_paula_volumes++;
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
    int i, control = 0;

    if (n == 1 && fds[0].sock == STUB_UDP && !interrupted) {
        fds[0].ready = udp_given < stub_udp_replies ? PLAT_READ : 0;
        if (!fds[0].ready)
            stub_now += timeout_ms;
        return fds[0].ready ? 1 : 0;
    }
    if (stub_waits == 0 || interrupted)
        return -1;                      /* as a Ctrl-C */
    if (stub_wait_advances)
        stub_now += timeout_ms;
    for (i = 0; i < n; i++) {
        fds[i].ready = stub_wait[0][fds[i].sock == STUB_STREAM] & fds[i].events;
        control |= fds[i].sock == STUB_CONTROL;
    }
    if (!control)
        stub_waits_without_control++;
    stub_waits--;
    memmove(stub_wait[0], stub_wait[1], sizeof(stub_wait) - sizeof(stub_wait[0]));
    return n;
}
