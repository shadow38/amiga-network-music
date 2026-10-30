/*
 * flac.c - portable core: the FLAC decoder, with dr_flac (dr_flac.c).
 *
 * LMS sends a FLAC file as it is: "fLaC", metadata blocks (STREAMINFO first; an embedded picture may weigh more
 * than the ring buffer), then the audio frames. The header is read here as it arrives: STREAMINFO is kept, every
 * other block skipped; dr_flac is then opened on a minimal header (STREAMINFO alone) followed by the frames.
 * After a seek (or when LMS resumes a track), LMS sends the file from a frame in the middle, without its header:
 * a STREAMINFO is then made up from the first frame's header (rate, channels, bits, block size).
 * dr_flac reads ahead (its cache) and takes a short read for the end of the stream, so a frame is only decoded
 * when the buffer holds a whole one (bound from STREAMINFO), or when the track's data is complete; and at most
 * one frame per request. Output: 16-bit samples (24 bits keep their 16 most significant bits), native byte order.
 */
#include <stdlib.h>
#include <string.h>

/* The same options as dr_flac.c: they shape dr_flac's structures, which are read here */
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#define DR_FLAC_NO_CRC
#include "dr_flac.h"

#include "decoder.h"
#include "platform.h"

#define HEAD_SIZE  42                       /* "fLaC", STREAMINFO block header (4 bytes) and body (34) */
#define FRAME_HEAD 16                       /* longest frame header */
#define BOUND_MAX  (256UL * 1024UL)         /* a frame bigger than this is not waited for */

enum phase { MAGIC, FRAME, BLOCK_HEADER, STREAMINFO, SKIP, OPEN, DECODE, FAILED };

struct flac_state {
    drflac  *flac;
    int      phase;                         /* enum phase */
    int      last;                          /* the metadata block being read is the last one */
    uint32_t skip;                          /* bytes of the current metadata block still to skip */
    uint8_t  head[HEAD_SIZE + FRAME_HEAD];  /* the minimal header given to dr_flac, then (after a seek) the start
                                               of the first frame, already taken from the buffer */
    uint32_t head_len;
    uint32_t head_pos;                      /* bytes of it already given */
    int      have_info;                     /* STREAMINFO read */
    int      done;                          /* everything taken from the buffer handed over: end of the track */
    uint32_t min_block;                     /* PCM frames per FLAC frame, smallest (but the last frame) */
    uint32_t bound;                         /* input wanted before decoding a frame */
    struct ring *r;                         /* input of the current run() */
    uint32_t avail;
};

/* dr_flac's input: the minimal header, then the frames in the ring buffer (never past the track's end) */
static size_t flac_read(void *user, void *buf, size_t n)
{
    struct flac_state *st = user;
    uint8_t *b = buf;
    size_t got = 0;

    if (st->head_pos < st->head_len) {
        got = st->head_len - st->head_pos < n ? st->head_len - st->head_pos : n;
        memcpy(b, st->head + st->head_pos, got);
        st->head_pos += (uint32_t)got;
    }
    if (got < n && st->avail > 0) {
        uint32_t k = n - got < st->avail ? (uint32_t)(n - got) : st->avail;

        k = ring_read(st->r, b + got, k);
        st->avail -= k;
        got += k;
    }
    return got;
}

/* Only forward skips of what is in the buffer */
static drflac_bool32 flac_seek(void *user, int offset, drflac_seek_origin origin)
{
    struct flac_state *st = user;

    if (origin != DRFLAC_SEEK_CUR || offset < 0 || (uint32_t)offset > st->avail)
        return DRFLAC_FALSE;
    ring_consume(st->r, (uint32_t)offset);
    st->avail -= (uint32_t)offset;
    return DRFLAC_TRUE;
}

static uint32_t get24(const uint8_t *b)
{
    return (uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2];
}

/* STREAMINFO body (34 bytes): block sizes, frame sizes, rate, channels, bits per sample */
static void read_streaminfo(struct decoder *d, struct flac_state *st, const uint8_t *s)
{
    uint32_t max_block = (uint32_t)s[2] << 8 | s[3];
    uint32_t max_frame = get24(s + 7);
    uint32_t rate = get24(s + 10) >> 4;
    uint32_t channels = ((s[12] >> 1) & 7) + 1;
    uint32_t bits = (((uint32_t)s[12] & 1) << 4 | s[13] >> 4) + 1;

    st->min_block = (uint32_t)s[0] << 8 | s[1];
    if (max_frame == 0)                     /* unknown: an uncompressed frame, at worst */
        max_frame = max_block * channels * ((bits + 7) / 8) + 1024;
    st->bound = max_frame + DR_FLAC_BUFFER_SIZE;
    if (st->bound > BOUND_MAX)
        st->bound = BOUND_MAX;
    d->out.rate = rate;
    d->out.channels = (uint8_t)channels;
    memcpy(st->head, "fLaC", 4);
    st->head[4] = 0x80;                     /* last metadata block, type 0 (STREAMINFO) */
    st->head[5] = 0;
    st->head[6] = 0;
    st->head[7] = 34;
    memcpy(st->head + 8, s, 34);
    st->head_len = HEAD_SIZE;
}

/*
 * A frame header (f, n bytes: the whole header when the frame is that long) turned into a STREAMINFO body (s, 34
 * bytes); 0 if it is not one, or if it leaves its rate or bits to the STREAMINFO block (not sent)
 */
static int frame_streaminfo(const uint8_t *f, uint32_t n, uint8_t *s)
{
    static const uint32_t rates[12] = { 0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000,
                                        96000 };
    static const uint8_t bits_table[8] = { 0, 8, 12, 0, 16, 20, 24, 0 };   /* 0: reserved, or in STREAMINFO */
    uint32_t pos = 5, block, rate, channels, bits, min_block, max_block;
    uint8_t code;

    if (n < 6 || f[0] != 0xFF || (f[1] & 0xFE) != 0xF8)
        return 0;
    for (code = f[4]; code & 0x80; code <<= 1)      /* coded frame number: 0xxxxxxx is 1 byte, 110xxxxx 2... */
        pos++;
    if (pos > 5)
        pos--;                                      /* ...11111110 7: as many bytes as leading ones */
    code = f[2] >> 4;
    if (code == 0)
        return 0;
    else if (code == 1)
        block = 192;
    else if (code <= 5)
        block = 576UL << (code - 2);
    else if (code == 6)
        block = pos < n ? f[pos++] + 1UL : 0;
    else if (code == 7) {
        block = pos + 1 < n ? ((uint32_t)f[pos] << 8 | f[pos + 1]) + 1 : 0;
        pos += 2;
    } else
        block = 256UL << (code - 8);
    code = f[2] & 15;
    if (code >= 1 && code <= 11)
        rate = rates[code];
    else if (code == 12)
        rate = pos < n ? f[pos] * 1000UL : 0;
    else if (code == 13 || code == 14) {
        rate = pos + 1 < n ? ((uint32_t)f[pos] << 8 | f[pos + 1]) : 0;
        if (code == 14)
            rate *= 10;
    } else
        rate = 0;                                   /* 0: in the STREAMINFO block; 15: invalid */
    code = f[3] >> 4;
    channels = code < 8 ? code + 1UL : code <= 10 ? 2 : 0;
    bits = bits_table[(f[3] >> 1) & 7];
    if (block == 0 || block > 65535 || rate == 0 || channels == 0 || bits == 0)   /* rate < 655360: fits 20 bits */
        return 0;
    if (f[1] & 1) {                                 /* variable block size: any size may follow */
        min_block = 16;
        max_block = 65535;
    } else                                          /* fixed: every frame has this size, but the last */
        min_block = max_block = block;
    memset(s, 0, 34);                               /* frame sizes, length and MD5: unknown */
    s[0] = (uint8_t)(min_block >> 8);
    s[1] = (uint8_t)min_block;
    s[2] = (uint8_t)(max_block >> 8);
    s[3] = (uint8_t)max_block;
    s[10] = (uint8_t)(rate >> 12);
    s[11] = (uint8_t)(rate >> 4);
    s[12] = (uint8_t)((rate & 15) << 4 | (channels - 1) << 1 | (bits - 1) >> 4);
    s[13] = (uint8_t)(((bits - 1) & 15) << 4);
    return 1;
}

/* The track cannot be decoded: what is left of it is dropped */
static void fail(struct flac_state *st, const char *why)
{
    msg("amisqueeze: flac: %s, track skipped\n", why);
    st->phase = FAILED;
}

/* Reads the header as far as the buffer allows; 1 once dr_flac is open */
static int read_header(struct decoder *d, struct flac_state *st, int complete)
{
    uint8_t b[34];

    while (st->phase != DECODE) {
        switch (st->phase) {
        case MAGIC:
            if (st->avail < 4)
                return 0;
            st->avail -= ring_read(st->r, b, 4);
            if (!memcmp(b, "fLaC", 4))
                st->phase = BLOCK_HEADER;
            else if (b[0] == 0xFF && (b[1] & 0xFE) == 0xF8) {
                memcpy(st->head + HEAD_SIZE, b, 4);  /* a frame: the file from the middle (a seek) */
                st->phase = FRAME;
            } else
                fail(st, "not a FLAC stream");
            break;
        case FRAME: {
            uint32_t k = FRAME_HEAD - 4;

            if (st->avail < k && !complete)
                return 0;
            if (k > st->avail)
                k = st->avail;
            st->avail -= ring_read(st->r, st->head + HEAD_SIZE + 4, k);
            if (!frame_streaminfo(st->head + HEAD_SIZE, 4 + k, b))
                fail(st, "cannot start from the middle of this file");
            else {
                read_streaminfo(d, st, b);
                st->head_len = HEAD_SIZE + 4 + k;   /* the frame's start follows the made-up header */
                st->have_info = 1;
                st->phase = OPEN;
            }
            break;
        }
        case BLOCK_HEADER:
            if (st->avail < 4)
                return 0;
            st->avail -= ring_read(st->r, b, 4);
            st->last = b[0] & 0x80;
            st->skip = get24(b + 1);
            st->phase = (b[0] & 0x7f) == 0 && st->skip == 34 && !st->have_info ? STREAMINFO : SKIP;
            break;
        case STREAMINFO:
            if (st->avail < 34)
                return 0;
            st->avail -= ring_read(st->r, b, 34);
            if (get24(b + 10) >> 4 == 0) {      /* the output would never move on */
                fail(st, "sample rate 0");
                break;
            }
            read_streaminfo(d, st, b);
            st->have_info = 1;
            st->phase = st->last ? OPEN : BLOCK_HEADER;
            break;
        case SKIP: {
            uint32_t k = st->skip < st->avail ? st->skip : st->avail;

            ring_consume(st->r, k);
            st->avail -= k;
            st->skip -= k;
            if (st->skip > 0)
                return 0;
            if (!st->last)
                st->phase = BLOCK_HEADER;
            else if (st->have_info)
                st->phase = OPEN;
            else
                fail(st, "no STREAMINFO block");
            break;
        }
        case OPEN:
            if (!complete && st->avail < st->bound)
                return 0;                       /* dr_flac reads the first frame's header as it opens */
            st->flac = drflac_open(flac_read, flac_seek, NULL, st, NULL);
            if (st->flac == NULL)
                fail(st, "cannot open the stream");
            else if (st->flac->channels > 2)
                fail(st, "more than 2 channels");
            else {
                if (st->min_block == 0 || st->min_block > st->flac->maxBlockSizeInPCMFrames)
                    st->min_block = st->flac->maxBlockSizeInPCMFrames;
                d->out.rate = st->flac->sampleRate;
                d->out.channels = st->flac->channels;
                st->phase = DECODE;
            }
            break;
        default:                                /* FAILED */
            ring_consume(st->r, st->avail);
            st->avail = 0;
            if (complete)
                st->done = 1;
            return 0;
        }
    }
    return 1;
}

static int flac_start(struct decoder *d, const struct pcm_format *in)
{
    struct flac_state *st = calloc(1, sizeof(*st));

    if (st == NULL) {
        msg("amisqueeze: flac: not enough memory\n");
        return 0;
    }
    d->state = st;
    st->phase = MAGIC;
    d->out.rate = in->rate;                     /* a guess until STREAMINFO */
    d->out.channels = 2;
    d->out.sample_bytes = 2;
    d->out.big_endian = (uint8_t)host_big_endian();
    return 1;
}

static void flac_stop(struct decoder *d)
{
    struct flac_state *st = d->state;

    if (st == NULL)
        return;
    if (st->flac != NULL)
        drflac_close(st->flac);
    free(st);
    d->state = NULL;
}

static uint32_t flac_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                         uint32_t out_max)
{
    struct flac_state *st = d->state;
    uint32_t frame, produced = 0;

    st->r = r;
    st->avail = avail;
    if (!read_header(d, st, complete)) {
        if (complete) {                         /* the track ends within its header */
            ring_consume(r, st->avail);
            st->avail = 0;
            st->done = 1;
        }
        return 0;
    }
    frame = (uint32_t)d->out.channels * 2;
    while (produced + frame <= out_max) {
        uint32_t remaining = st->flac->currentFLACFrame.pcmFramesRemaining;
        drflac_uint64 want = (out_max - produced) / frame, got;

        if (remaining > 0) {
            if (want > remaining)
                want = remaining;               /* what is left of the frame decoded already */
        } else {
            if (!complete && st->avail < st->bound)
                break;                          /* wait for a whole frame in the buffer */
            if (want > st->min_block)
                want = st->min_block;           /* one frame at most */
        }
        got = drflac_read_pcm_frames_s16(st->flac, want, (drflac_int16 *)(out + produced));
        if (got == 0) {
            if (complete) {                     /* end of the track: what follows the last frame is dropped */
                ring_consume(r, st->avail);
                st->avail = 0;
                st->done = 1;
            }
            break;
        }
        produced += (uint32_t)got * frame;
    }
    return produced;
}

static uint32_t flac_input_for(const struct decoder *d, uint32_t out)
{
    const struct flac_state *st = d->state;

    (void)out;
    return st != NULL && st->bound ? 2 * st->bound : 64UL * 1024UL;
}

static uint32_t flac_buffered(const struct decoder *d)
{
    const struct flac_state *st = d->state;

    if (st == NULL || st->phase != DECODE)
        return 0;
    return st->flac->currentFLACFrame.pcmFramesRemaining * (uint32_t)d->out.channels * 2;
}

static int flac_finished(const struct decoder *d)
{
    const struct flac_state *st = d->state;

    return st == NULL || st->done;
}

const struct decoder_ops decoder_flac = { "flac", flac_start, flac_stop, flac_run, flac_input_for, flac_buffered,
                                          flac_finished };
