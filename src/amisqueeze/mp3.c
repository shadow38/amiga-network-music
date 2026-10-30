/*
 * mp3.c - portable core: the MP3 decoder, with minimp3 (minimp3.c). Only in the builds with
 * AMISQUEEZE_MP3: minimp3 computes in floating point, too slow without an FPU.
 *
 * LMS sends an MP3 file as it is, from its start (ID3v2 tags first: an embedded picture may weigh more than the
 * ring buffer) or, after a seek, from its first frame then anywhere in the middle. Tags are skipped as they arrive;
 * a LAME header in the first frame gives the samples to drop at the start and the end (gapless). The
 * frames go through a staging buffer, where minimp3 finds them. It wants the frame to decode and the header of the
 * next one (otherwise it starts over and loses the bit reservoir), and, to find the first frame, several frames in
 * a row: so it is called when the staging buffer is full (first frame) or holds the biggest frame and a header, or
 * when the track's data is complete. A decoded frame (up to 1152 PCM frames) is handed over as the output asks for it.
 * Output: 16-bit samples, native byte order; rate and channels taken from the frames.
 */
#ifdef AMISQUEEZE_MP3

#include <stdlib.h>
#include <string.h>

/* The same options as minimp3.c */
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "decoder.h"
#include "platform.h"

#define STAGE_SIZE  (16UL * 1024UL)         /* the first frame is looked for in this much: 15 frames at 320 kbit/s */
#define FRAME_MAX   2304UL                  /* biggest frame minimp3 accepts (free format) */
#define FRAME_NEED  (FRAME_MAX + 4)         /* a frame and the next frame's header */
#define ID3_HEADER  10
#define ID3V1_SIZE  128
#define DECODER_DELAY 529                   /* PCM frames an MP3 decoder outputs late (the synthesis filter) */
#define NO_LIMIT    UINT64_MAX

enum phase { TAGS, FRAMES };

struct mp3_state {
    mp3dec_t dec;
    int      phase;                         /* enum phase */
    uint32_t skip;                          /* bytes of the current ID3v2 tag still to skip */
    int      synced;                        /* a frame has been decoded: the next ones follow it */
    int      decoded;                       /* frames decoded in the track */
    int      done;                          /* everything taken from the buffer handed over: end of the track */
    int      tail_checked;                  /* the end of the track has been looked at for an ID3v1 tag */
    uint32_t pos, end;                      /* bytes waiting in stage: [pos, end) */
    uint32_t pcm_pos, pcm_len;              /* decoded bytes of pcm still to hand over: [pcm_pos, pcm_len) */
    uint32_t pcm_rate;                      /* format of pcm */
    uint8_t  pcm_channels;
    int      first_seen;                    /* the first frame has been looked at for a LAME header */
    uint32_t lead;                          /* PCM frames still to drop at the start (encoder and decoder delay) */
    uint64_t left;                          /* PCM frames of the track still to play (LAME header), or NO_LIMIT */
    uint8_t  stage[STAGE_SIZE];
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

/* Size of the ID3v2 tag at b (ID3_HEADER bytes): header, body (28-bit "synchsafe" size) and footer */
static uint32_t id3_size(const uint8_t *b)
{
    uint32_t size = ID3_HEADER + ((uint32_t)(b[6] & 0x7f) << 21 | (uint32_t)(b[7] & 0x7f) << 14 |
                                  (uint32_t)(b[8] & 0x7f) << 7 | (b[9] & 0x7f));

    return b[5] & 0x10 ? size + ID3_HEADER : size;
}

static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

/* Gapless playback: an encoder such as LAME writes in the first frame a header, "Xing" or "Info"
 * after the side information, with the number of audio frames, then its own extension with its delay and padding
 * (12 bits each, 21 bytes into it); the layout is the one minimp3_ex.h reads. lead gets the encoder's delay and
 * the decoder's, left the samples of the track; a frame count of 0 (a broken header) sets no limit, so that the
 * track is not dropped whole. After a seek, LMS sends this frame again before the data from the new position:
 * left is then never reached, and nothing is cut at the end. (See decoder.h.) */
int mp3_lame_header(const uint8_t *f, uint32_t size, uint32_t *lead, uint64_t *left)
{
    int mpeg1 = (f[1] & 0x18) == 0x18;
    int mono = (f[3] >> 6) == 3;
    uint32_t off = 4 + ((f[1] & 1) ? 0 : 2) + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
    uint32_t flags, frames;
    int32_t delay, padding;
    uint64_t total;

    if (size < off + 8 || (memcmp(f + off, "Xing", 4) && memcmp(f + off, "Info", 4)))
        return 0;
    flags = f[off + 7];
    if (!(flags & 1))
        return 1;                           /* no frame count: a header, nothing known to trim */
    if (size < off + 12)
        return 0;
    frames = get32(f + off + 8);
    off += 12;
    if (flags & 2)
        off += 4;                           /* bytes */
    if (flags & 4)
        off += 100;                         /* seek table */
    if (flags & 8)
        off += 4;                           /* quality */
    if (size < off + 1)
        return 0;
    if (f[off] == 0)
        return 1;                           /* no encoder extension: delay and padding unknown */
    if (size < off + 36)
        return 0;
    delay = ((int32_t)f[off + 21] << 4 | f[off + 22] >> 4) + DECODER_DELAY;
    padding = ((int32_t)(f[off + 22] & 0x0f) << 8 | f[off + 23]) - DECODER_DELAY;
    total = (uint64_t)frames * (mpeg1 ? 1152 : 576);
    if (total >= (uint64_t)delay)
        total -= (uint64_t)delay;
    if (padding > 0 && total >= (uint64_t)padding)
        total -= (uint64_t)padding;
    *lead = (uint32_t)delay;
    if (frames > 0)
        *left = total;
    return 1;
}

/* Moves what waits in stage to its start, so that it can be filled further */
static void compact(struct mp3_state *st)
{
    memmove(st->stage, st->stage + st->pos, st->end - st->pos);
    st->end -= st->pos;
    st->pos = 0;
}

static int mp3_start(struct decoder *d, const struct pcm_format *in)
{
    struct mp3_state *st = calloc(1, sizeof(*st));

    if (st == NULL) {
        msg("amisqueeze: mp3: not enough memory\n");
        return 0;
    }
    d->state = st;
    mp3dec_init(&st->dec);
    st->phase = TAGS;
    st->left = NO_LIMIT;
    d->out.rate = in->rate;                     /* a guess until the first frame */
    d->out.channels = 2;
    d->out.sample_bytes = 2;
    d->out.big_endian = (uint8_t)host_big_endian();
    return 1;
}

static void mp3_stop(struct decoder *d)
{
    free(d->state);
    d->state = NULL;
}

/* Takes input: skips what is left of a tag, then fills stage. 1 if the track's data is all in stage (or skipped). */
static int take_input(struct mp3_state *st, struct ring *r, uint32_t *avail, int complete)
{
    uint32_t k;

    if (st->skip > 0) {
        k = st->skip < *avail ? st->skip : *avail;
        ring_consume(r, k);
        *avail -= k;
        st->skip -= k;
    }
    if (st->skip == 0 && *avail > 0) {
        if (st->pos >= STAGE_SIZE / 2 || (st->pos > 0 && !st->synced))
            compact(st);
        k = STAGE_SIZE - st->end < *avail ? STAGE_SIZE - st->end : *avail;
        k = ring_read(r, st->stage + st->end, k);
        *avail -= k;
        st->end += k;
    }
    return complete && *avail == 0 && st->skip == 0;
}

/* Skips the ID3v2 tags at the start of the stream (there may be several); 1 once the frames begin */
static int skip_tags(struct mp3_state *st, int all_in)
{
    while (st->skip == 0) {
        uint32_t size, k;

        if (st->end - st->pos < ID3_HEADER)
            return all_in;                      /* a few bytes: not a tag, or not yet known */
        if (memcmp(st->stage + st->pos, "ID3", 3))
            return 1;
        size = id3_size(st->stage + st->pos);
        k = size < st->end - st->pos ? size : st->end - st->pos;
        st->pos += k;
        st->skip = size - k;
    }
    return 0;
}

/* Hands over decoded PCM, whole PCM frames; returns the bytes written */
static uint32_t hand_out(struct decoder *d, struct mp3_state *st, uint8_t *out, uint32_t out_max)
{
    uint32_t frame = (uint32_t)d->out.channels * 2;
    uint32_t n = st->pcm_len - st->pcm_pos;

    if (n > out_max)
        n = out_max - out_max % frame;
    memcpy(out, (const uint8_t *)st->pcm + st->pcm_pos, n);
    st->pcm_pos += n;
    return n;
}

static uint32_t mp3_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                        uint32_t out_max)
{
    struct mp3_state *st = d->state;
    uint32_t produced = 0;

    for (;;) {
        mp3dec_frame_info_t info;
        const uint8_t *frame;
        uint32_t waiting;
        int samples, all_in;

        if (st->pcm_pos < st->pcm_len) {
            if (st->pcm_rate != d->out.rate || st->pcm_channels != d->out.channels) {
                if (produced > 0)
                    break;                      /* a new format: in the next block */
                d->out.rate = st->pcm_rate;
                d->out.channels = st->pcm_channels;
            }
            produced += hand_out(d, st, out + produced, out_max - produced);
            if (st->pcm_pos < st->pcm_len)
                break;                          /* the output is full */
        }
        if (st->done || out_max - produced < (uint32_t)d->out.channels * 2)
            break;
        all_in = take_input(st, r, &avail, complete);
        if (st->phase == TAGS) {
            if (!skip_tags(st, all_in))
                break;                          /* a tag still arriving */
            st->phase = FRAMES;
        }
        if (all_in && !st->tail_checked) {
            st->tail_checked = 1;
            if (st->end - st->pos >= ID3V1_SIZE && !memcmp(st->stage + st->end - ID3V1_SIZE, "TAG", 3))
                st->end -= ID3V1_SIZE;          /* an ID3v1 tag ends the file: not taken for a frame */
        }
        waiting = st->end - st->pos;
        if (waiting == 0 || (!all_in && waiting < (st->synced ? FRAME_NEED : STAGE_SIZE))) {
            if (all_in) {                       /* end of the track */
                if (st->decoded == 0)
                    msg("amisqueeze: mp3: no MP3 frame found, track skipped\n");
                st->pos = st->end = 0;
                st->done = 1;
            }
            break;
        }
        memset(&info, 0, sizeof(info));
        samples = mp3dec_decode_frame(&st->dec, st->stage + st->pos, (int)waiting, st->pcm, &info);
        /* Bytes used: a frame, or what comes before the first frame found (all of them when there is none) */
        if (info.frame_bytes <= 0 || (uint32_t)info.frame_bytes > waiting)
            info.frame_bytes = (int)waiting;    /* not expected: nothing usable */
        frame = st->stage + st->pos + info.frame_offset;
        st->pos += (uint32_t)info.frame_bytes;
        if (info.hz != 0 && !st->first_seen) {  /* the first frame found */
            st->first_seen = 1;
            if (mp3_lame_header(frame, (uint32_t)(info.frame_bytes - info.frame_offset), &st->lead, &st->left)) {
                st->synced = 1;
                st->decoded++;
                continue;                       /* a header, not audio */
            }
        }
        if (samples > 0) {
            uint32_t n = (uint32_t)samples, drop = st->lead < n ? st->lead : n, frame_size;

            st->synced = 1;
            st->decoded++;
            st->pcm_rate = (uint32_t)info.hz;
            st->pcm_channels = (uint8_t)info.channels;
            frame_size = (uint32_t)info.channels * 2;
            st->lead -= drop;                   /* the delay at the start of the track */
            n -= drop;
            if (st->left != NO_LIMIT) {         /* the padding at its end */
                if (n > st->left)
                    n = (uint32_t)st->left;
                st->left -= n;
            }
            st->pcm_pos = drop * frame_size;
            st->pcm_len = st->pcm_pos + n * frame_size;
        }                                       /* 0 samples: bytes skipped, or a frame without its reservoir */
    }
    return produced;
}

static uint32_t mp3_input_for(const struct decoder *d, uint32_t out)
{
    const struct mp3_state *st = d->state;

    /* At most 320 kbit/s against 16-bit stereo PCM at 44.1 kHz (1411 kbit/s): a quarter */
    return (st != NULL && st->synced ? 2 * FRAME_NEED : STAGE_SIZE) + out / 4;
}

static uint32_t mp3_buffered(const struct decoder *d)
{
    const struct mp3_state *st = d->state;

    return st == NULL ? 0 : st->pcm_len - st->pcm_pos;
}

static int mp3_finished(const struct decoder *d)
{
    const struct mp3_state *st = d->state;

    return st == NULL || (st->done && st->pcm_pos == st->pcm_len);
}

const struct decoder_ops decoder_mp3 = { "mp3", mp3_start, mp3_stop, mp3_run, mp3_input_for, mp3_buffered,
                                         mp3_finished };

#endif
