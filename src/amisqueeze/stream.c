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
 * The ring buffer holds the stream as received; the decoder of each track (decoder.h; raw PCM is copied as is)
 * turns it into PCM for a sink: the platform's audio output (AHI, blocks managed by audio.c), or the virtual
 * output, which consumes it at the PCM rate like a sound card (host build, NOAUDIO, OUTPUT); with the OUTPUT
 * option, the decoded PCM is also written to a file (plan step 1.3, checked byte by byte). Track boundaries,
 * elapsed time and the end of playback follow the bytes of decoded PCM the sink has played.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

static void stop_decoder(struct player *p);

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
    stop_decoder(p);
    ring_free(&p->ring);
}

/* Empties the buffer and the sink, forgets the queued tracks: positions start again from 0 */
static void output_reset(struct player *p)
{
    ring_clear(&p->ring);
    if (p->audio.enabled)
        audio_reset(p);
    p->out.queued = 0;
    p->out.cur.start = 0;
    p->out.cur.out_start = 0;
    p->out.carry = 0;
    p->out.handed = 0;
    stop_decoder(p);                    /* the decoder starts again with the next track */
}

void stream_stop(struct player *p)
{
    stream_close(p);
    p->stream.state = STREAM_IDLE;
    output_reset(p);
    p->out.state = OUTPUT_STOPPED;
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
                  uint32_t threshold, int autostart, const struct pcm_format *fmt, const struct decoder_ops *ops)
{
    struct stream *s = &p->stream;
    struct output *o = &p->out;

    if (request_len > (int)sizeof(s->request)) {
        msg("amisqueeze: stream: request too long (%d bytes)\n", request_len);
        return;
    }
    stream_close(p);                    /* a stream still running is abandoned */
    if (o->state == OUTPUT_STOPPED)     /* nothing playing: start from an empty buffer */
        output_reset(p);
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
    o->queue[o->queued].fmt = *fmt;
    o->queue[o->queued].ops = ops;
    o->queue[o->queued].out_fmt = *fmt;     /* until the decoder says otherwise */
    o->queue[o->queued].out_start = OUT_START_UNKNOWN;
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

/* ---- output ---- */

uint32_t pcm_bytes_per_sec(const struct pcm_format *f)
{
    return f->rate * f->channels * f->sample_bytes;
}

/* ---- sink: where the output's bytes go (the audio output, or the virtual output) ---- */

/* Bytes the sink takes now. Virtual output: the bytes due since the last tick, at the PCM rate of the data handed
 * over next. */
static uint32_t sink_space(struct player *p, const struct pcm_format *f, uint32_t now)
{
    struct output *o = &p->out;
    uint64_t want = (uint64_t)(uint32_t)(now - o->last_tick) * pcm_bytes_per_sec(f) + o->carry;

    o->last_tick = now;
    o->carry = (uint32_t)(want % 1000);
    return (uint32_t)(want / 1000);
}

/* Bytes of decoded PCM played since the last output_reset(), with an estimate of the audio block playing now.
 * Virtual output: everything handed over is played at once. */
static uint64_t sink_played(const struct player *p)
{
    if (p->audio.enabled)
        return audio_played(p, plat_now_ms(), 1);
    return p->out.handed;
}

/* 1 when everything handed over was played */
static int sink_drained(const struct player *p)
{
    return !p->audio.enabled || audio_pending(p) == 0;
}

static int streaming(const struct player *p)
{
    int s = p->stream.state;

    return s == STREAM_CONNECTING || s == STREAM_HEADERS || s == STREAM_BODY;
}

/* 1 while the decoder has not handed over everything it took from the buffer */
static int decoding(const struct player *p)
{
    return p->out.dec.ops != NULL && !p->out.dec.ops->finished(&p->out.dec);
}

/* Track of the data at the buffer's read position, the next one to decode */
static struct track *read_track(struct player *p)
{
    struct output *o = &p->out;
    struct track *t = &o->cur;
    int i;

    for (i = 0; i < o->queued && o->queue[i].start <= p->ring.total_out; i++)
        t = &o->queue[i];
    return t;
}

/* The reading has reached the start of a queued track and the decoder holds nothing more of the previous one:
 * that track's output starts at what has been handed so far */
static void reach_tracks(struct player *p)
{
    struct output *o = &p->out;
    int i;

    if (decoding(p))
        return;
    for (i = 0; i < o->queued && o->queue[i].start <= p->ring.total_out; i++)
        if (o->queue[i].out_start == OUT_START_UNKNOWN)
            o->queue[i].out_start = o->handed;
}

/* 1 when the output has played everything before the next queued track (its start in the output is known once
 * the reading has reached it) */
static int next_track_due(struct player *p)
{
    const struct output *o = &p->out;

    reach_tracks(p);
    return o->queued > 0 && o->queue[0].out_start != OUT_START_UNKNOWN && sink_played(p) >= o->queue[0].out_start;
}

/* At most n bytes, stopping where the next track begins (what the sink takes never spans two tracks) */
static uint32_t until_next_track(const struct player *p, uint32_t n)
{
    const struct output *o = &p->out;
    int i;

    for (i = 0; i < o->queued; i++)
        if (o->queue[i].start > p->ring.total_out) {
            if (o->queue[i].start - p->ring.total_out < n)
                n = (uint32_t)(o->queue[i].start - p->ring.total_out);
            break;
        }
    return n;
}

/* Leaves the track being decoded: the decoder frees its state */
static void stop_decoder(struct player *p)
{
    struct output *o = &p->out;

    if (o->dec.ops != NULL)
        o->dec.ops->stop(&o->dec);
    o->dec.ops = NULL;
}

/* Track to decode now: the one whose decoder has not finished (its input may be used up already), otherwise the
 * one at the buffer's read position */
static struct track *decode_track(struct player *p)
{
    struct output *o = &p->out;
    int i;

    if (decoding(p)) {
        if (o->cur.start == o->dec.track)
            return &o->cur;
        for (i = 0; i < o->queued; i++)
            if (o->queue[i].start == o->dec.track)
                return &o->queue[i];
    }
    return read_track(p);
}

/* Decoder of a track whose own decoder could not start: drops its data, writes nothing */
static int skip_start(struct decoder *d, const struct pcm_format *in)
{
    (void)d;
    (void)in;
    return 1;
}

static void skip_stop(struct decoder *d)
{
    (void)d;
}

static uint32_t skip_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                         uint32_t out_max)
{
    (void)d;
    (void)complete;
    (void)out;
    (void)out_max;
    ring_consume(r, avail);
    return 0;
}

static uint32_t skip_none(const struct decoder *d)
{
    (void)d;
    return 0;
}

static uint32_t skip_input_for(const struct decoder *d, uint32_t out)
{
    (void)out;
    return skip_none(d);
}

static int skip_finished(const struct decoder *d)
{
    (void)d;
    return 1;
}

static const struct decoder_ops decoder_skip = { "skip", skip_start, skip_stop, skip_run, skip_input_for,
                                                 skip_none, skip_finished };

/* Starts the decoder on track t when the decoding reaches it: its first decoded byte is the output position
 * handed so far. A track the decoder cannot take is skipped (its data dropped). 0 if there is nothing to decode
 * for now. */
static int prepare_decoder(struct player *p, struct track *t)
{
    struct output *o = &p->out;

    if (o->dec.ops == t->ops && o->dec.track == t->start)
        return 1;
    stop_decoder(p);
    o->dec.ops = t->ops;
    o->dec.track = t->start;
    o->dec.state = NULL;
    o->dec.verbose = (uint8_t)(p->verbose != 0);
    if (t->out_start == OUT_START_UNKNOWN)
        t->out_start = o->handed;
    if (t->ops->start(&o->dec, &t->fmt)) {
        t->out_fmt = o->dec.out;
        return 1;
    }
    msg("amisqueeze: %s: cannot decode this track, skipped\n", t->ops->name);
    ring_consume(&p->ring, until_next_track(p, ring_used(&p->ring)));
    t->ops = o->dec.ops = &decoder_skip;    /* drops the rest of the track as it comes, without a state */
    o->dec.out = t->out_fmt;
    return 0;
}

/* Bytes of track t that are in the buffer, and whether that is all of them (the next track follows in the buffer,
 * or the stream has ended). A track the decoder has not finished, though the reading has passed it, has no input
 * left. */
static uint32_t track_input(struct player *p, const struct track *t, int *complete)
{
    uint32_t used = ring_used(&p->ring), avail = until_next_track(p, used);

    if (t != read_track(p)) {
        *complete = 1;
        return 0;
    }
    *complete = avail < used || !streaming(p);
    return avail;
}

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

/* Virtual output: n bytes of decoded PCM played, written to the OUTPUT file if any. A write error closes the file;
 * playback goes on without it. */
static void hand_over(struct player *p, const uint8_t *d, uint32_t n)
{
    struct output *o = &p->out;

    if (o->file >= 0) {
        int w = plat_file_write(o->file, d, (int)n);
        if (w > 0)
            o->written += (uint32_t)w;
        if (w != (int)n) {
            msg("amisqueeze: cannot write to %s: error %d, file closed\n", o->file_name, plat_error());
            output_file_close(p);
        }
    }
    o->handed += n;
}

/* The output reaches the next queued track: it becomes the current one */
static void next_track(struct player *p)
{
    struct output *o = &p->out;

    o->cur = o->queue[0];
    if (o->cur.out_start == OUT_START_UNKNOWN)  /* the first track: nothing decoded yet */
        o->cur.out_start = o->handed;
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

/* Buffer and sink empty: STMo while still streaming (the network is too slow), STMu once everything was played */
static void output_empty(struct player *p)
{
    struct output *o = &p->out;

    o->carry = 0;
    if (streaming(p)) {
        if (!o->starved) {
            o->starved = 1;
            if (p->verbose)
                msg("output: buffer empty, waiting for data\n");
            slim_send_stat(p, "STMo", 0);
        }
    } else {
        o->state = OUTPUT_STOPPED;
        if (p->verbose)
            msg("output: end of playback\n");
        slim_send_stat(p, "STMu", 0);
    }
}

/* Audio output: the decoder fills the free blocks. A block holds whole frames of one track; it is sent full, or
 * with what there is when the track's data is complete or when the device is about to run dry (fewer than two
 * blocks queued). 8-bit samples fill half a block: they double in size when converted to 16 bits. */
static void feed_audio(struct player *p, uint32_t now)
{
    struct output *o = &p->out;

    for (;;) {
        struct track *t;
        uint32_t frame, n, avail, full;
        uint64_t before;
        int b, complete;

        if (next_track_due(p)) {
            next_track(p);
            continue;
        }
        if (ring_used(&p->ring) == 0 && !decoding(p)) {
            if (sink_drained(p))
                output_empty(p);
            return;
        }
        b = audio_free_block(p);
        if (b < 0)
            return;
        t = decode_track(p);
        if (!prepare_decoder(p, t))
            continue;
        frame = (uint32_t)t->out_fmt.channels * t->out_fmt.sample_bytes;
        full = t->out_fmt.sample_bytes == 1 ? AUDIO_BLOCK_SIZE / 2 : AUDIO_BLOCK_SIZE;
        full -= full % frame;
        avail = track_input(p, t, &complete);
        if (!complete && audio_pending(p) >= 2 && o->dec.ops->buffered(&o->dec) == 0 &&
            avail < o->dec.ops->input_for(&o->dec, full))
            return;                         /* more of this track to come: wait to fill a whole block */
        before = p->ring.total_out;
        n = o->dec.ops->run(&o->dec, &p->ring, avail, complete, audio_block_data(p, b), full);
        t->out_fmt = o->dec.out;            /* known once the decoder has read the stream's header */
        if (n == 0) {
            if (p->ring.total_out != before)
                continue;                   /* consumed without output: header, or an end not decodable */
            if (ring_used(&p->ring) == 0 && sink_drained(p))
                output_empty(p);            /* the decoder waits for data that has not come */
            return;
        }
        o->starved = 0;
        audio_submit(p, b, n, &t->out_fmt, now);
        o->handed += n;
    }
}

/* Virtual output: hands over the bytes due since the last tick, decoded through a small buffer */
static void feed_virtual(struct player *p, uint32_t now)
{
    static uint16_t pcm[2048];              /* 16-bit aligned: decoders write native samples */
    struct output *o = &p->out;
    struct track *t = decode_track(p);
    uint32_t bytes = sink_space(p, &t->out_fmt, now);

    for (;;) {
        uint32_t n, avail;
        uint64_t before;
        int complete;

        if (next_track_due(p)) {
            next_track(p);
            continue;
        }
        if (bytes == 0)
            break;
        if (ring_used(&p->ring) == 0 && !decoding(p)) {
            output_empty(p);
            break;
        }
        t = decode_track(p);
        if (!prepare_decoder(p, t))
            continue;
        avail = track_input(p, t, &complete);
        before = p->ring.total_out;
        n = o->dec.ops->run(&o->dec, &p->ring, avail, complete, (uint8_t *)pcm,
                            bytes < sizeof(pcm) ? bytes : sizeof(pcm));
        t->out_fmt = o->dec.out;            /* known once the decoder has read the stream's header */
        if (n == 0) {
            if (p->ring.total_out != before)
                continue;                   /* consumed without output: header, or an end not decodable */
            if (bytes < (uint32_t)t->out_fmt.channels * t->out_fmt.sample_bytes)
                o->carry += bytes * 1000;   /* less than a frame due: kept for the next tick */
            else if (ring_used(&p->ring) == 0)
                output_empty(p);            /* the decoder waits for data that has not come */
            break;
        }
        o->starved = 0;
        hand_over(p, (const uint8_t *)pcm, n);
        bytes -= n;
    }
}

void output_tick(struct player *p, uint32_t now)
{
    struct output *o = &p->out;

    if (o->state != OUTPUT_RUNNING) {
        o->last_tick = now;
        return;
    }
    if (p->audio.enabled)
        audio_poll(p, now);
    /* Track boundaries already played */
    while (next_track_due(p))
        next_track(p);
    if (p->audio.enabled)
        feed_audio(p, now);
    else
        feed_virtual(p, now);

    if (o->state == OUTPUT_RUNNING && (uint32_t)(now - o->last_stmt) >= 1000) {
        o->last_stmt = now;
        slim_send_stat(p, "STMt", 0);
    }
}

void output_pause(struct player *p)
{
    if (p->out.state == OUTPUT_RUNNING) {
        p->out.state = OUTPUT_PAUSED;
        if (p->audio.enabled)
            audio_pause(p, plat_now_ms());  /* the sound stops at once */
    }
}

void output_resume(struct player *p)
{
    struct output *o = &p->out;

    if (o->state == OUTPUT_PAUSED || o->state == OUTPUT_LOADED) {
        if (o->state == OUTPUT_PAUSED && p->audio.enabled)
            audio_resume(p, plat_now_ms());
        o->state = OUTPUT_RUNNING;
        o->last_tick = o->last_stmt = plat_now_ms();
        o->carry = 0;
    }
}

uint32_t output_elapsed_ms(const struct player *p)
{
    const struct track *t = &p->out.cur;
    uint32_t bps = pcm_bytes_per_sec(&t->out_fmt);
    uint64_t played = sink_played(p);

    if (bps == 0 || t->out_start == OUT_START_UNKNOWN || played < t->out_start)
        return 0;
    return (uint32_t)((played - t->out_start) * 1000 / bps);
}

int player_busy(const struct player *p)
{
    return p->stream.sock >= 0 || p->out.state == OUTPUT_RUNNING || audio_pending(p) > 0;
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
