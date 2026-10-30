/*
 * decoder.c - portable core: the raw PCM decoder. The PCM goes to the output as the server sent
 * it (any sample size and byte order: the audio output converts it); one copy, ring buffer to destination.
 *
 * A track that begins with a WAV header (Music Assistant's wav output codec, whose strm s gives
 * default values) is played in the header's format; LMS strips the headers itself. The header is read again from
 * the start at each call until its data chunk is in the buffer, then consumed in a call that returns no PCM: the
 * output sizes its next block from the new format.
 */
#include <string.h>

#include "decoder.h"
#include "platform.h"

#define WAV_HEADER_MAX 4096                 /* bytes before the data chunk */
#define WAV_RATE_MAX 384000                 /* Hz; keeps the bytes per second (at most 8 per frame) within 32 bits */

enum { PCM_HEADER, PCM_DATA, PCM_DROP };   /* d->phase */
enum { WAV_NONE, WAV_WAIT, WAV_FOUND, WAV_BAD };

int host_big_endian(void)
{
    const uint16_t one = 1;

    return *(const uint8_t *)&one == 0;
}

static uint32_t le16(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8;
}

static uint32_t le32(const uint8_t *b)
{
    return le16(b) | le16(b + 2) << 16;
}

static int wav_bad(const char *why)
{
    msg("amisqueeze: wav: %s, track skipped\n", why);
    return WAV_BAD;
}

/* The format chunk (b, n bytes of it): f set if amisqueeze can play it */
static int wav_format(const uint8_t *b, uint32_t n, struct pcm_format *f)
{
    /* the GUID of the PCM sub-format after its first two bytes (KSDATAFORMAT_SUBTYPE_PCM) */
    static const uint8_t pcm_guid[14] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
    uint32_t tag = le16(b), channels = le16(b + 2), rate = le32(b + 4), bits = le16(b + 14);

    if (tag == 0xfffe && (n < 40 || le16(b + 24) != 1 || memcmp(b + 26, pcm_guid, sizeof(pcm_guid)) != 0))
        return wav_bad("not integer PCM");      /* WAVE_FORMAT_EXTENSIBLE, sub-format not PCM */
    if (tag != 1 && tag != 0xfffe)
        return wav_bad("not integer PCM");      /* floating point (3), compressed */
    if (bits % 8 != 0 || bits < 8 || bits > 32)
        return wav_bad("sample size not supported");
    if (channels < 1 || channels > 2)
        return wav_bad("number of channels not supported");
    if (rate == 0 || rate > WAV_RATE_MAX)
        return wav_bad("sample rate not supported");
    f->rate = rate;
    f->channels = (uint8_t)channels;
    f->sample_bytes = (uint8_t)(bits / 8);
    f->big_endian = 0;
    return WAV_FOUND;
}

/* Looks for a WAV header in the first avail bytes of the track: if found, sets f and *skip (the bytes up to the
 * data) and the data size */
static int wav_header(struct decoder *d, const struct ring *r, uint32_t avail, int complete, struct pcm_format *f,
                      uint32_t *skip)
{
    uint8_t b[40];
    uint32_t off = 12, len;
    int have_format = 0;

    if (avail < 12)
        return complete ? WAV_NONE : WAV_WAIT;
    ring_peek(r, 0, b, 12);
    if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0)
        return WAV_NONE;
    for (;;) {
        if (avail < off + 8)
            return complete ? wav_bad("the track ends in its header") : WAV_WAIT;
        ring_peek(r, off, b, 8);
        len = le32(b + 4);
        if (memcmp(b, "data", 4) == 0) {
            if (!have_format)
                return wav_bad("no format chunk");
            *skip = off + 8;
            d->limited = len != 0xffffffffUL && len != 0x7fffeffcUL;   /* or unknown: to the end of the stream */
            d->data_left = len;
            return WAV_FOUND;
        }
        if (len > WAV_HEADER_MAX || off + 8 + len > WAV_HEADER_MAX)
            return wav_bad("header too long");
        if (memcmp(b, "fmt ", 4) == 0) {
            uint32_t n = len < sizeof(b) ? len : sizeof(b);

            if (len < 16)
                return wav_bad("format chunk too short");
            if (avail < off + 8 + n)
                return complete ? wav_bad("the track ends in its header") : WAV_WAIT;
            ring_peek(r, off + 8, b, n);
            if (wav_format(b, n, f) != WAV_FOUND)
                return WAV_BAD;
            have_format = 1;
        }
        off += 8 + len + (len & 1);         /* chunks of odd size are followed by a pad byte */
    }
}

static int pcm_start(struct decoder *d, const struct pcm_format *in)
{
    d->out = *in;
    d->in_frame = (uint32_t)in->channels * in->sample_bytes;
    d->phase = PCM_HEADER;
    d->limited = 0;
    return d->in_frame > 0;
}

static void pcm_stop(struct decoder *d)
{
    (void)d;
}

/* The start of the track: 1 to go on with its data, 0 to return (more input needed, or the header consumed) */
static int pcm_header(struct decoder *d, struct ring *r, uint32_t avail, int complete)
{
    struct pcm_format f;
    uint32_t skip = 0;

    switch (wav_header(d, r, avail, complete, &f, &skip)) {
    case WAV_WAIT:
        return 0;
    case WAV_NONE:
        d->phase = PCM_DATA;
        return 1;
    case WAV_FOUND:
        if (d->verbose) {
            if (d->limited)
                msg("stream: WAV header: %u bits, %lu Hz, %u channel(s), %lu bytes\n", (unsigned)f.sample_bytes * 8,
                    (unsigned long)f.rate, (unsigned)f.channels, (unsigned long)d->data_left);
            else
                msg("stream: WAV header: %u bits, %lu Hz, %u channel(s), to the end of the stream\n",
                    (unsigned)f.sample_bytes * 8, (unsigned long)f.rate, (unsigned)f.channels);
        }
        d->out = f;
        d->in_frame = (uint32_t)f.channels * f.sample_bytes;
        ring_consume(r, skip);
        d->phase = PCM_DATA;
        return 0;
    default:
        d->phase = PCM_DROP;
        return 1;
    }
}

static uint32_t pcm_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                        uint32_t out_max)
{
    uint32_t n;

    if (d->phase == PCM_HEADER && !pcm_header(d, r, avail, complete))
        return 0;
    if (d->phase == PCM_DROP) {
        ring_consume(r, avail);
        return 0;
    }
    n = avail;
    if (d->limited && n > d->data_left)
        n = d->data_left;
    if (n > out_max)
        n = out_max;
    n -= n % d->in_frame;
    if (n == 0) {
        if (d->limited && d->data_left < d->in_frame)
            ring_consume(r, avail);         /* past the WAV data (other chunks, a partial frame): dropped */
        else if (complete && avail > 0 && avail < d->in_frame)
            ring_consume(r, avail);         /* a partial frame at the end of the track: dropped */
        return 0;
    }
    n = ring_read(r, out, n);
    if (d->limited)
        d->data_left -= n;
    return n;
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
