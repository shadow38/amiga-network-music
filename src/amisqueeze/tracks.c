/*
 * tracks.c - portable core: the queue of tracks in the ring buffer, and the decoder of each
 *
 * Each strm s queues a track at the ring's write position (stream.c). The decoding follows the ring's read
 * position: a track's decoder starts when the reading reaches it and never reads past the track's end; a track
 * whose decoder cannot start gets the skip decoder, which drops its data. Where a track begins in the output is
 * known once the decoder holds nothing more of the track before it (architecture.md, Two kinds of position).
 */
#include <stddef.h>

#include "platform.h"
#include "player.h"

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

int decoding(const struct player *p)
{
    return p->out.dec.ops != NULL && !p->out.dec.ops->finished(&p->out.dec);
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

int next_track_due(struct player *p)
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

void stop_decoder(struct player *p)
{
    struct output *o = &p->out;

    if (o->dec.ops != NULL)
        o->dec.ops->stop(&o->dec);
    o->dec.ops = NULL;
}

struct track *decode_track(struct player *p)
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

int prepare_decoder(struct player *p, struct track *t)
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

uint32_t track_input(struct player *p, const struct track *t, int *complete)
{
    uint32_t used = ring_used(&p->ring), avail = until_next_track(p, used);

    if (t != read_track(p)) {
        *complete = 1;
        return 0;
    }
    *complete = avail < used || !stream_active(p);
    return avail;
}
