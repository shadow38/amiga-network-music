/*
 * stream.c - portable core: the HTTP stream of the last track queued, into the ring buffer
 *
 * Behaviour expected by the server (as in squeezelite, rewritten from the protocol description):
 *   strm s -> STMf, non-blocking connection, request sent as is, STMc; response headers -> RESP;
 *   body -> ring buffer; the output starts once `threshold` bytes are buffered
 *     (autostart 1: STMs; autostart 0: STMl, then strm u);
 *   autostart 3 and 2 (a direct stream: LMS's internet radios, every track of Music Assistant): after RESP the body
 *     waits for cont, which gives the interval of the radio's metadata blocks (icy.c, each one sent back in META),
 *     then as 1 and 0;
 *   a status outside 2xx: nothing played; autostart 0 and 1 send DSCO 2, with 2 and 3 the server decides;
 *   end of the HTTP body -> DSCO + STMd: the server then sends the next track, appended to the buffer, and
 *     STMs is sent again when the output reaches it (gapless);
 *   buffer empty: STMo while still streaming, STMu once everything was played;
 *   strm f (LMS: the next track removed, moved or reshuffled) drops the last strm s's track, not the one playing:
 *     its stream is closed without DSCO, its data dropped, and the next strm s follows the track before it; a
 *     track refused or failed without data is already gone, and strm f then drops nothing;
 *   strm s while a stream still runs: that stream is closed, its track dropped if no data of it came yet;
 *   strm s refused (format not decoded, request too long, queue of tracks full) -> STMc + STMn: the server moves on
 *     to the next track.
 * The ring buffer holds the stream as received, several tracks back to back; tracks.c follows the queue of tracks
 * and the decoder of each, output.c hands the decoded PCM over to a sink.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

/* While the body streams, the socket is read only once this much of the buffer is free (at most a quarter of it):
 * the virtual output frees a few hundred bytes at each turn of the loop, and reading them at once kept the loop
 * awake (WinUAE, 68020 at 28 MHz: 700 to 900 reads of 200 bytes a second, 40 % of the CPU with NOAUDIO) */
#define READ_MIN_SPACE (8UL * 1024UL)

int stream_init(struct player *p)
{
    memset(&p->stream, 0, sizeof(p->stream));
    p->stream.sock = -1;
    p->stream.state = STREAM_IDLE;
    output_init(p);
    return ring_init(&p->ring, STREAM_BUFFER_SIZE);
}

static void stream_close(struct player *p)
{
    if (p->stream.sock >= 0) {
        plat_close(p->stream.sock);
        p->stream.sock = -1;
        p->stream.closes++;             /* the next socket may take the same number */
    }
}

void stream_free(struct player *p)
{
    stream_close(p);
    stop_decoder(p);
    ring_free(&p->ring);
}

void stream_stop(struct player *p)
{
    stream_close(p);
    p->stream.state = STREAM_IDLE;
    output_reset(p);
    p->out.state = OUTPUT_STOPPED;
    p->out.starved = 0;
}

void stream_flush(struct player *p)
{
    struct output *o = &p->out;
    struct track *t;

    if (o->state != OUTPUT_RUNNING && o->state != OUTPUT_PAUSED) {
        stream_stop(p);                 /* nothing playing: the track dropped is the only one */
        return;
    }
    stream_close(p);                    /* no DSCO: the server does not wait for the end of that stream */
    p->stream.state = STREAM_IDLE;
    if (!p->stream.in_queue || o->queued == 0)  /* playing, it plays what it received; refused or failed, gone */
        return;
    p->stream.in_queue = 0;
    t = &o->queue[--o->queued];         /* the track the last strm s queued */
    if (o->dec.ops != NULL && o->dec.track == t->start)
        stop_decoder(p);                /* audio output: its first blocks, already filled, end the track playing */
    ring_truncate(&p->ring, t->start);  /* its data, what the decoding has not taken yet */
}

/* The stream ends early, in the given state: a track without any data is dropped */
static void stream_abandon(struct player *p, int state)
{
    struct output *o = &p->out;

    stream_close(p);
    p->stream.state = state;
    if (p->stream.in_queue && o->queued > 0 && o->queue[o->queued - 1].start == p->ring.total_in) {
        o->queued--;
        p->stream.in_queue = 0;
    }
}

/* The stream ends early (connection failed, error): DSCO with the reason */
static void stream_fail(struct player *p, int reason)
{
    stream_abandon(p, STREAM_IDLE);
    slim_send_dsco(p, reason);
}

/* A strm s refused before anything changes, the tracks in the buffer going on: STMc + STMn. The queue is full only
 * when the server keeps sending tracks before the one waiting has started (protocol.md, A track's life);
 * forgetting the oldest instead would leave its data to the decoder of the track before it. */
static int start_refused(struct player *p, int request_len)
{
    if (request_len > (int)sizeof(p->stream.request))
        msg("amisqueeze: stream: request too long (%d bytes)\n", request_len);
    else if (p->out.queued == MAX_TRACKS)   /* never with the output stopped: a strm s then empties the queue */
        msg("amisqueeze: stream: %d tracks already waiting, this one skipped\n", MAX_TRACKS);
    else
        return 0;
    slim_send_refusal(p);
    return 1;
}

void stream_start(struct player *p, uint32_t ip, uint16_t port, const uint8_t *request, int request_len,
                  uint32_t threshold, int autostart, const struct pcm_format *fmt, const struct decoder_ops *ops)
{
    struct stream *s = &p->stream;
    struct output *o = &p->out;

    if (start_refused(p, request_len))
        return;
    if (s->sock >= 0)                   /* a stream still running is abandoned, its track dropped if it has no data */
        stream_abandon(p, STREAM_IDLE);
    if (o->state == OUTPUT_STOPPED)    /* nothing playing: start from an empty buffer */
        output_reset(p);
    memcpy(s->request, request, (size_t)request_len);
    s->request_len = request_len;
    s->ip = ip;
    s->port = port;
    s->headers_len = 0;
    s->headers_end = 0;
    s->bytes = 0;
    s->threshold = threshold;
    s->autostart = autostart;
    s->again = 0;
    s->again_said = 0;
    icy_start(&s->icy, 0);              /* blocks only once a cont asks for them */

    o->queue[o->queued].start = p->ring.total_in;
    o->queue[o->queued].fmt = *fmt;
    o->queue[o->queued].ops = ops;
    o->queue[o->queued].out_fmt = *fmt;     /* until the decoder says otherwise */
    o->queue[o->queued].out_start = OUT_START_UNKNOWN;
    o->queued++;
    s->in_queue = 1;

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
    if (s->again && (int32_t)(plat_now_ms() - s->again_until) < 0)
        return 0;
    switch (s->state) {
    case STREAM_CONNECTING:
        return PLAT_WRITE;
    case STREAM_HEADERS:                /* body bytes may follow the headers in the same read */
        return ring_space(&p->ring) >= sizeof(s->headers) ? PLAT_READ : 0;
    case STREAM_BODY: {
        uint32_t min = p->ring.size / 4 < READ_MIN_SPACE ? p->ring.size / 4 : READ_MIN_SPACE;

        return ring_space(&p->ring) >= (min > 0 ? min : 1) ? PLAT_READ : 0;
    }
    default:
        return 0;
    }
}

int stream_active(const struct player *p)
{
    int s = p->stream.state;

    return s == STREAM_CONNECTING || s == STREAM_HEADERS || s == STREAM_CONT || s == STREAM_BODY;
}

void stream_late(struct player *p, uint32_t now)
{
    struct stream *s = &p->stream;

    if (s->state != STREAM_CONT || s->cont_said || (uint32_t)(now - s->cont_since) < CONT_SAY_MS)
        return;
    s->cont_said = 1;
    msg("amisqueeze: stream: no cont from the server after %d s, still waiting\n", CONT_SAY_MS / 1000);
}

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

/* Reported readable, nothing to read: left out of the waits for a while */
static void read_again(struct player *p)
{
    struct stream *s = &p->stream;

    if (p->verbose && !s->again_said)
        msg("stream: reported readable, nothing to read yet\n");
    s->again_said = 1;
    s->again = 1;
    s->again_until = plat_now_ms() + AGAIN_WAIT_MS;
}

/* A complete metadata block: passed on to the server, its title shown with VERBOSE */
static void block_received(struct player *p)
{
    static char title[80];
    const struct icy *m = &p->stream.icy;

    slim_send_meta(p, m->block, m->block_len);
    if (!p->verbose)
        return;
    if (icy_title(m->block, m->block_len, title, sizeof(title)))
        msg("stream: title: %s\n", title);
    else
        msg("stream: metadata without a title (%lu bytes)\n", (unsigned long)m->block_len);
}

/* Body bytes received with the headers: through the metadata blocks into the ring (the headers were read only
 * with room for them in the ring) */
static void take_held(struct player *p)
{
    struct stream *s = &p->stream;
    const uint8_t *d = (const uint8_t *)s->headers + s->headers_end;
    uint32_t left = (uint32_t)(s->headers_len - s->headers_end);

    while (left > 0) {
        uint32_t n = icy_sound(&s->icy, left);

        if (n > 0) {
            store(p, d, n);
            icy_took_sound(&s->icy, n);
            s->bytes += n;
        } else {
            uint8_t *w = icy_meta_space(&s->icy, &n);

            if (n > left)
                n = left;
            memcpy(w, d, n);
            if (icy_took_meta(&s->icy, n))
                block_received(p);
        }
        d += n;
        left -= n;
    }
    s->headers_len = s->headers_end;
}

/* A status outside 2xx: the body never enters the buffer. Autostart 0 or 1: the stream fails. Autostart 2 or 3:
 * the server, which has the headers in RESP, decides (LMS follows a redirect itself); a DSCO now would count as a
 * second failure, or against the next track. */
static void refuse(struct player *p, int status)
{
    if (status < 0)
        msg("amisqueeze: stream: no HTTP status in the response, nothing played\n");
    else
        msg("amisqueeze: stream: HTTP status %d, nothing played from this address\n", status);
    if (p->stream.autostart < 2)
        stream_fail(p, DSCO_REMOTE);
    else
        stream_abandon(p, STREAM_REFUSED);
}

static void read_headers(struct player *p)
{
    struct stream *s = &p->stream;
    int room = (int)sizeof(s->headers) - 1 - s->headers_len;
    int n, end, status;

    if (room <= 0) {
        msg("amisqueeze: stream: response headers too long\n");
        stream_fail(p, DSCO_LOCAL);
        return;
    }
    n = plat_recv(s->sock, s->headers + s->headers_len, room);
    if (n == PLAT_AGAIN) {
        read_again(p);
        return;
    }
    s->again = 0;
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
    status = http_status(s->headers, end);
    if (status < 200 || status > 299) {
        refuse(p, status);
        return;
    }
    s->headers_end = end;
    if (s->autostart >= 2) {            /* body bytes received with the headers wait in s->headers */
        s->state = STREAM_CONT;
        s->cont_since = plat_now_ms();
        s->cont_said = 0;
        if (p->verbose)
            msg("stream: waiting for cont\n");
        return;
    }
    s->state = STREAM_BODY;
    take_held(p);
    output_check_start(p);
}

void stream_cont(struct player *p, uint32_t interval)
{
    struct stream *s = &p->stream;

    if (s->state == STREAM_REFUSED) {   /* Music Assistant sends cont whatever the status */
        s->state = STREAM_IDLE;
        slim_send_dsco(p, DSCO_REMOTE);
        return;
    }
    if (s->state != STREAM_CONT) {
        if (p->verbose)
            msg("stream: cont with no stream waiting for it, ignored\n");
        return;
    }
    icy_start(&s->icy, interval);
    s->state = STREAM_BODY;
    take_held(p);
    output_check_start(p);
}

/* Each recv stops where the next metadata block begins: the sound goes straight into the ring, the block into its
 * own buffer */
static void read_body(struct player *p)
{
    struct stream *s = &p->stream;
    uint8_t *w;
    uint32_t room = ring_write_space(&p->ring, &w), want;
    int n, meta;

    if (room == 0)
        return;
    want = icy_sound(&s->icy, room > 65536 ? 65536 : room);
    meta = want == 0;
    if (meta)                           /* a block's length byte, or the block */
        w = icy_meta_space(&s->icy, &want);
    n = plat_recv(s->sock, w, (int)want);
    if (n == PLAT_AGAIN) {
        read_again(p);
        return;
    }
    s->again = 0;
    if (n > 0) {
        if (meta) {
            if (icy_took_meta(&s->icy, (uint32_t)n))
                block_received(p);
            return;
        }
        ring_commit(&p->ring, (uint32_t)n);
        icy_took_sound(&s->icy, (uint32_t)n);
        s->bytes += (uint32_t)n;
        output_check_start(p);
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
    output_check_start(p);              /* short track: all of it arrived before the threshold */
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