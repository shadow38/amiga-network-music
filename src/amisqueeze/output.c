/*
 * output.c - portable core: the output, which hands the decoded PCM over to a sink
 *
 * The sink is the platform's audio output (AHI, blocks managed by audio.c), or the virtual output, which consumes
 * the PCM at its rate like a sound card (host build, NOAUDIO, OUTPUT); with the OUTPUT option, the decoded PCM is
 * also written to a file (the tests compare it byte by byte). Track boundaries, elapsed time and the end of
 * playback follow the bytes of decoded PCM the sink has played: STMs at each track started, STMt every second, STMo
 * when the buffer runs dry while still streaming, STMu once everything was played.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

void output_init(struct player *p)
{
    memset(&p->out, 0, sizeof(p->out));
    p->out.state = OUTPUT_STOPPED;
    p->out.file = -1;
}

void output_reset(struct player *p)
{
    ring_clear(&p->ring);
    if (p->audio.enabled)
        audio_reset(p);
    p->out.queued = 0;
    p->stream.in_queue = 0;
    p->out.cur.start = 0;
    p->out.cur.out_start = 0;
    p->out.carry = 0;
    p->out.handed = 0;
    stop_decoder(p);                    /* the decoder starts again with the next track */
}

uint32_t pcm_bytes_per_sec(const struct pcm_format *f)
{
    return f->rate * f->channels * f->sample_bytes;
}

/* ---- sink: where the output's bytes go (the audio output, or the virtual output) ---- */

/* Bytes the sink takes now. Virtual output: the bytes due since the last tick, at the PCM rate of the data handed
 * over next: the rate strm s announced, until the decoder has read the track's header. */
static uint32_t sink_space(struct player *p, const struct pcm_format *f, uint32_t now)
{
    struct output *o = &p->out;
    uint64_t want = (uint64_t)(uint32_t)(now - o->last_tick) * pcm_bytes_per_sec(f) + o->carry;

    o->last_tick = now;
    o->carry = (uint32_t)(want % 1000);
    return (uint32_t)(want / 1000);
}

uint64_t sink_played(const struct player *p)
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

/* ---- output ---- */

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
    if (o->queued == 0)                 /* the last strm s's track is playing */
        p->stream.in_queue = 0;
    if (p->verbose)
        msg("output: track started\n");
    slim_send_stat(p, "STMs", 0);
}

void output_check_start(struct player *p)
{
    struct stream *s = &p->stream;
    struct output *o = &p->out;

    if (o->state != OUTPUT_STOPPED || o->queued == 0)  /* the callers never come in STREAM_CONT */
        return;
    if (s->bytes < s->threshold && s->state != STREAM_DONE)
        return;
    if (s->autostart & 1) {             /* 1, or 3 once cont has come */
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

/* Buffer and sink empty, or nothing in the buffer the decoder can use yet (the start of a frame whose rest has not
 * come): STMo while still streaming (the network is too slow), STMu once everything was played */
static void output_empty(struct player *p)
{
    struct output *o = &p->out;

    o->carry = 0;
    if (stream_active(p)) {
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
 * blocks queued). How much a block takes: audio_block_room(). */
static void feed_audio(struct player *p, uint32_t now)
{
    struct output *o = &p->out;

    for (;;) {
        struct track *t;
        uint32_t n, avail, full;
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
        full = audio_block_room(p, &t->out_fmt);
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
            /* The decoder waits for data that has not come, maybe the rest of a frame; a complete track only
             * ends its decoding (the next track may be in the ring already) */
            if ((!complete || ring_used(&p->ring) == 0) && sink_drained(p))
                output_empty(p);
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
            else if (!complete || ring_used(&p->ring) == 0)
                output_empty(p);            /* the decoder waits for data that has not come (as above) */
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

    stream_late(p, now);
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
