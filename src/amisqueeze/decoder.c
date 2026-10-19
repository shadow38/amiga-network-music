/*
 * decoder.c - portable core: the raw PCM decoder (plan step 1.8a). The PCM goes to the output as the server sent
 * it (any sample size and byte order: the audio output converts it); one copy, ring buffer to destination.
 */
#include "decoder.h"

int host_big_endian(void)
{
    const uint16_t one = 1;

    return *(const uint8_t *)&one == 0;
}

static int pcm_start(struct decoder *d, const struct pcm_format *in)
{
    d->out = *in;
    d->in_frame = (uint32_t)in->channels * in->sample_bytes;
    return d->in_frame > 0;
}

static void pcm_stop(struct decoder *d)
{
    (void)d;
}

static uint32_t pcm_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                        uint32_t out_max)
{
    uint32_t n = avail < out_max ? avail : out_max;

    n -= n % d->in_frame;
    if (n == 0) {
        if (complete && avail > 0 && avail < d->in_frame)
            ring_consume(r, avail);         /* a partial frame at the end of the track: dropped */
        return 0;
    }
    return ring_read(r, out, n);
}

static uint32_t pcm_input_for(const struct decoder *d, uint32_t out)
{
    (void)d;
    return out;
}

static uint32_t pcm_buffered(const struct decoder *d)
{
    (void)d;
    return 0;
}

static int pcm_finished(const struct decoder *d)
{
    (void)d;
    return 1;                               /* what it reads is handed over at once */
}

const struct decoder_ops decoder_pcm = { "pcm", pcm_start, pcm_stop, pcm_run, pcm_input_for, pcm_buffered,
                                         pcm_finished };
