/*
 * stream.c - portable core: HTTP stream of the current track, ring buffer and virtual output (plan step 1.2)
 *
 * Behaviour expected by the server (as in squeezelite, rewritten from the protocol description):
 *   strm s -> STMf, non-blocking connection, request sent as is, STMc; response headers -> RESP;
 *   body -> ring buffer; the output starts once `threshold` bytes are buffered
 *     (autostart 1: STMs; autostart 0: STMl, then strm u);
 *   end of the HTTP body -> DSCO + STMd: the server then sends the next track, appended to the buffer, and
 *     STMs is sent again when the output reaches it (gapless);
 *   buffer empty: STMo while still streaming, STMu once everything was played.
 * Single buffer: raw PCM needs no decoding, so the ring buffer is both the stream and the output buffer.
 * The virtual output consumes it at the PCM rate, like a sound card; with the OUTPUT option, what it consumes is
 * also written to a file (plan step 1.3: what AHI will play, checked byte by byte). AHI comes with step 1.4.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

int stream_init(struct player *p)
{
    memset(&p->stream, 0, sizeof(p->stream));
    memset(&p->out, 0, sizeof(p->out));
    p->stream.sock = -1;
    p->stream.state = STREAM_IDLE;
    p->out.state = OUTPUT_STOPPED;
    p->out.file = -1;
    return ring_init(&p->ring, STREAM_BUFFER_SIZE);
}

static void stream_close(struct player *p)
{
    if (p->stream.sock >= 0) {
        plat_close(p->stream.sock);
        p->stream.sock = -1;
    }
}

void stream_free(struct player *p)
{
    stream_close(p);
    ring_free(&p->ring);
}

void stream_stop(struct player *p)
{
    stream_close(p);
    p->stream.state = STREAM_IDLE;
    ring_clear(&p->ring);
    p->out.state = OUTPUT_STOPPED;
    p->out.queued = 0;
    p->out.played = 0;
    p->out.starved = 0;
}

/* The stream ends early (connection failed, error): DSCO with the reason; a track without any data is dropped */
static void stream_fail(struct player *p, int reason)
{
    struct output *o = &p->out;

    stream_close(p);
    p->stream.state = STREAM_IDLE;
    if (o->queued > 0 && o->queue[o->queued - 1].start == p->ring.total_in)
        o->queued--;
    slim_send_dsco(p, reason);
}

void stream_start(struct player *p, uint32_t ip, uint16_t port, const uint8_t *request, int request_len,
                  uint32_t threshold, int autostart, uint32_t bytes_per_sec)
{
    struct stream *s = &p->stream;
    struct output *o = &p->out;

    if (request_len > (int)sizeof(s->request)) {
        msg("amisqueeze: stream: request too long (%d bytes)\n", request_len);
        return;
    }
    stream_close(p);                    /* a stream still running is abandoned */
    if (o->state == OUTPUT_STOPPED) {   /* nothing playing: start from an empty buffer */
        ring_clear(&p->ring);
        o->queued = 0;
    }
    memcpy(s->request, request, (size_t)request_len);
    s->request_len = request_len;
    s->ip = ip;
    s->port = port;
    s->headers_len = 0;
    s->bytes = 0;
    s->threshold = threshold;
    s->autostart = autostart;

    if (o->queued == MAX_TRACKS) {      /* should not happen: forget the oldest */
        memmove(o->queue, o->queue + 1, (MAX_TRACKS - 1) * sizeof(o->queue[0]));
        o->queued--;
    }
    o->queue[o->queued].start = p->ring.total_in;
    o->queue[o->queued].bytes_per_sec = bytes_per_sec;
    o->queued++;

    s->sock = plat_connect_start(ip, port);
    if (s->sock < 0) {
        s->sock = -1;
        msg("amisqueeze: stream: cannot connect: error %d\n", plat_error());
        stream_fail(p, DSCO_UNREACHABLE);
        return;
    }
    s->state = STREAM_CONNECTING;
}

int stream_events(const struct player *p)
{
    const struct stream *s = &p->stream;

    if (s->sock < 0)
        return 0;
    switch (s->state) {
    case STREAM_CONNECTING:
        return PLAT_WRITE;
    case STREAM_HEADERS:                /* body bytes may follow the headers in the same read */
        return ring_space(&p->ring) >= sizeof(s->headers) ? PLAT_READ : 0;
    case STREAM_BODY:
        return ring_space(&p->ring) > 0 ? PLAT_READ : 0;
    default:
        return 0;
    }
}

/* ---- virtual output ---- */

int output_file_open(struct player *p, const char *name)
{
    struct output *o = &p->out;

    o->file = plat_file_create(name);
    if (o->file < 0) {
        msg("amisqueeze: cannot create %s: error %d\n", name, plat_error());
        return 0;
    }
    o->file_name = name;
    o->written = 0;
    if (p->verbose)
        msg("output: writing the audio to %s\n", name);
    return 1;
}

void output_file_close(struct player *p)
{
    struct output *o = &p->out;

    if (o->file < 0)
        return;
    plat_file_close(o->file);
    o->file = -1;
    if (p->verbose)
        msg("output: %lu bytes written to %s\n", (unsigned long)o->written, o->file_name);
}

/* Plays n bytes (n <= ring_used): written to the OUTPUT file if any, then dropped from the buffer. A write error
 * closes the file; playback goes on without it. */
static void consume(struct player *p, uint32_t n)
{
    struct output *o = &p->out;

    while (n > 0) {
        const uint8_t *d;
        uint32_t len = ring_read_space(&p->ring, &d);

        if (len > n)
            len = n;
        if (o->file >= 0) {
            int w = plat_file_write(o->file, d, (int)len);
            if (w > 0)
                o->written += (uint32_t)w;
            if (w != (int)len) {
                msg("amisqueeze: cannot write to %s: error %d, file closed\n", o->file_name, plat_error());
                output_file_close(p);
            }
        }
        ring_consume(&p->ring, len);
        n -= len;
    }
}

/* The output reaches the next queued track: it becomes the current one */
static void next_track(struct player *p)
{
    struct output *o = &p->out;

    o->bytes_per_sec = o->queue[0].bytes_per_sec;
    o->played = 0;
    o->queued--;
    memmove(o->queue, o->queue + 1, (size_t)o->queued * sizeof(o->queue[0]));
    if (p->verbose)
        msg("output: track started\n");
    slim_send_stat(p, "STMs", 0);
}

/* Starts the output once enough of the first track is buffered */
static void check_start(struct player *p)
{
    struct stream *s = &p->stream;
    struct output *o = &p->out;

    if (o->state != OUTPUT_STOPPED || o->queued == 0)
        return;
    if (s->bytes < s->threshold && s->state != STREAM_DONE)
        return;
    if (s->autostart) {
        o->state = OUTPUT_RUNNING;
        o->last_tick = o->last_stmt = plat_now_ms();
        o->carry = 0;
        o->starved = 0;
        next_track(p);
    } else {
        o->state = OUTPUT_LOADED;
        if (p->verbose)
            msg("output: loaded, waiting for strm u\n");
        slim_send_stat(p, "STMl", 0);
    }
}

void output_tick(struct player *p, uint32_t now)
{
    struct output *o = &p->out;
    struct ring *r = &p->ring;
    uint64_t want;
    uint32_t bytes;

    if (o->state != OUTPUT_RUNNING) {
        o->last_tick = now;
        return;
    }
    want = (uint64_t)(uint32_t)(now - o->last_tick) * o->bytes_per_sec + o->carry;
    o->last_tick = now;
    bytes = (uint32_t)(want / 1000);
    o->carry = (uint32_t)(want % 1000);

    for (;;) {
        uint32_t avail, n;

        if (o->queued > 0 && r->total_out >= o->queue[0].start) {
            next_track(p);
            continue;
        }
        if (bytes == 0)
            break;
        avail = ring_used(r);
        if (o->queued > 0 && o->queue[0].start - r->total_out < avail)
            avail = (uint32_t)(o->queue[0].start - r->total_out);
        if (avail == 0) {               /* buffer empty */
            o->carry = 0;
            if (p->stream.state == STREAM_CONNECTING || p->stream.state == STREAM_HEADERS ||
                p->stream.state == STREAM_BODY) {
                if (!o->starved) {      /* still streaming: the network is too slow */
                    o->starved = 1;
                    if (p->verbose)
                        msg("output: buffer empty, waiting for data\n");
                    slim_send_stat(p, "STMo", 0);
                }
            } else {                    /* everything was played */
                o->state = OUTPUT_STOPPED;
                if (p->verbose)
                    msg("output: end of playback\n");
                slim_send_stat(p, "STMu", 0);
            }
            break;
        }
        o->starved = 0;
        n = bytes < avail ? bytes : avail;
        consume(p, n);
        o->played += n;
        bytes -= n;
    }

    if (o->state == OUTPUT_RUNNING && (uint32_t)(now - o->last_stmt) >= 1000) {
        o->last_stmt = now;
        slim_send_stat(p, "STMt", 0);
    }
}

void output_pause(struct player *p)
{
    if (p->out.state == OUTPUT_RUNNING)
        p->out.state = OUTPUT_PAUSED;
}

void output_resume(struct player *p)
{
    struct output *o = &p->out;

    if (o->state == OUTPUT_PAUSED || o->state == OUTPUT_LOADED) {
        o->state = OUTPUT_RUNNING;
        o->last_tick = o->last_stmt = plat_now_ms();
        o->carry = 0;
    }
}

uint32_t output_elapsed_ms(const struct player *p)
{
    if (p->out.bytes_per_sec == 0)
        return 0;
    return (uint32_t)(p->out.played * 1000 / p->out.bytes_per_sec);
}

int player_busy(const struct player *p)
{
    return p->stream.sock >= 0 || p->out.state == OUTPUT_RUNNING;
}

/* ---- HTTP stream ---- */

int http_header_end(const char *buf, int len)
{
    int i;

    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n')
            return i + 2;
        if (i + 3 < len && buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    return 0;
}

/* Copies bytes into the ring buffer (the caller checked the space) */
static void store(struct player *p, const uint8_t *data, uint32_t len)
{
    while (len > 0) {
        uint8_t *w;
        uint32_t n = ring_write_space(&p->ring, &w);
        if (n > len)
            n = len;
        memcpy(w, data, n);
        ring_commit(&p->ring, n);
        data += n;
        len -= n;
    }
}

static void read_headers(struct player *p)
{
    struct stream *s = &p->stream;
    int room = (int)sizeof(s->headers) - 1 - s->headers_len;
    int n, end;

    if (room <= 0) {
        msg("amisqueeze: stream: response headers too long\n");
        stream_fail(p, DSCO_LOCAL);
        return;
    }
    n = plat_recv(s->sock, s->headers + s->headers_len, room);
    if (n <= 0) {
        msg("amisqueeze: stream: connection lost before the response headers\n");
        stream_fail(p, DSCO_REMOTE);
        return;
    }
    s->headers_len += n;
    s->headers[s->headers_len] = '\0';
    end = http_header_end(s->headers, s->headers_len);
    if (end == 0)
        return;
    if (p->verbose)
        msg("stream: response headers received (%d bytes)\n", end);
    slim_send_resp(p, s->headers, end);
    s->state = STREAM_BODY;
    if (s->headers_len > end) {         /* body bytes received with the headers */
        uint32_t extra = (uint32_t)(s->headers_len - end);
        store(p, (const uint8_t *)s->headers + end, extra);
        s->bytes += extra;
    }
    s->headers_len = end;
    check_start(p);
}

static void read_body(struct player *p)
{
    struct stream *s = &p->stream;
    uint8_t *w;
    uint32_t room = ring_write_space(&p->ring, &w);
    int n;

    if (room == 0)
        return;
    n = plat_recv(s->sock, w, room > 65536 ? 65536 : (int)room);
    if (n > 0) {
        ring_commit(&p->ring, (uint32_t)n);
        s->bytes += (uint32_t)n;
        check_start(p);
        return;
    }
    stream_close(p);
    s->state = STREAM_DONE;
    if (n == 0) {
        if (p->verbose)
            msg("stream: end of the track data (%lu bytes)\n", (unsigned long)s->bytes);
        slim_send_dsco(p, DSCO_OK);
    } else {
        msg("amisqueeze: stream: recv: error %d\n", plat_error());
        slim_send_dsco(p, DSCO_REMOTE);
    }
    slim_send_stat(p, "STMd", 0);
    check_start(p);                     /* short track: all of it arrived before the threshold */
}

void stream_io(struct player *p, int ready)
{
    struct stream *s = &p->stream;

    if (s->sock < 0)
        return;
    if (s->state == STREAM_CONNECTING) {
        const uint8_t *req = s->request;
        int left = s->request_len;

        if (!(ready & PLAT_WRITE))
            return;
        if (!plat_connect_finish(s->sock)) {
            msg("amisqueeze: stream: cannot connect: error %d\n", plat_error());
            stream_fail(p, DSCO_UNREACHABLE);
            return;
        }
        while (left > 0) {
            int n = plat_send(s->sock, req, left);
            if (n <= 0) {
                msg("amisqueeze: stream: send: error %d\n", plat_error());
                stream_fail(p, DSCO_LOCAL);
                return;
            }
            req += n;
            left -= n;
        }
        s->state = STREAM_HEADERS;
        if (p->verbose)
            msg("stream: connected, request sent\n");
        slim_send_stat(p, "STMc", 0);
        return;
    }
    if (!(ready & PLAT_READ))
        return;
    if (s->state == STREAM_HEADERS)
        read_headers(p);
    else if (s->state == STREAM_BODY)
        read_body(p);
}
