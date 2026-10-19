/*
 * audio.c - portable core: the blocks of the audio output (plan step 1.4b)
 *
 * The platform's audio device (ahi.device on the Amiga, see platform.h) plays the blocks one after the other.
 * Here: which block holds what and in which order, the conversion of the stream's PCM for the device (8, 16, 24 or
 * 32-bit samples to 16-bit big-endian, left and right swapped with SWAP), the bytes played (counted in bytes of the
 * stream, whatever the device got), the pause, and the volume.
 *
 * ahi.device chains a request only to one that is already playing: a request linked to a request that is itself
 * waiting is never played (seen in WinUAE, and in AHI's source: the third one is left in the waiting list). So at
 * most two blocks are with the device, the one playing and the next one linked to it; the third block is filled
 * in advance and sent as soon as the first one ends.
 * AHI's CMD_STOP only holds back the requests sent after it (and those of every program): a pause aborts the
 * blocks with the device and sends again, on resume, what they had not played yet.
 * The volume is applied to the samples here, as a block is sent for the first time; the device always gets full
 * volume: with ahi.device 6.6 and paula.audio, requests sent again after an abort (a resume) with a low ahir_Volume
 * corrupted the free memory list (seen in WinUAE, plan step 1.6).
 */
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "player.h"

#define AUDIO_SENT_MAX 2                    /* blocks with the device at the same time */

int audio_open(struct player *p, int unit, int swap)
{
    struct audio *a = &p->audio;
    int i;

    memset(a, 0, sizeof(*a));
    a->swap = swap;
    a->volume = AUDIO_VOLUME_FULL;
    a->tables = AUDIO_VOLUME_FULL;          /* not built yet (not needed at full volume) */
    for (i = 0; i < AUDIO_BLOCKS; i++) {
        a->block[i].buf = malloc(AUDIO_BLOCK_SIZE);
        if (a->block[i].buf == NULL) {
            msg("amisqueeze: not enough memory for the audio blocks\n");
            audio_close(p);
            return 0;
        }
    }
    if (!plat_audio_open(unit)) {
        audio_close(p);
        return 0;
    }
    a->enabled = 1;
    return 1;
}

void audio_close(struct player *p)
{
    struct audio *a = &p->audio;
    int i;

    if (a->enabled) {
        audio_reset(p);
        plat_audio_close();
        a->enabled = 0;
    }
    for (i = 0; i < AUDIO_BLOCKS; i++) {
        free(a->block[i].buf);
        a->block[i].buf = NULL;
    }
}

/* Blocks with the device: always the first ones of the playing order */
static int sent_count(const struct audio *a)
{
    int k = 0;

    while (k < a->count && a->block[a->order[k]].state == BLOCK_SENT)
        k++;
    return k;
}

/* Volume tables: a 16-bit sample is hi * 256 + lo (hi signed, lo unsigned), so sample * volume = high[hi] +
 * low[lo], without a multiplication per sample (a 32-bit one costs about 43 cycles on a 68020: a third of a 28 MHz
 * CPU at 44.1 kHz stereo). Built again when the volume changes. */
static void build_tables(struct audio *a)
{
    int32_t v = (int32_t)a->volume;
    int i;

    for (i = 0; i < 256; i++) {
        a->high[i] = (int32_t)(int8_t)i * 256 * v;    /* |hi * 256| <= 2^15, v < 2^16: no overflow */
        a->low[i] = i * v;
    }
    a->tables = a->volume;
}

/* In place: 8, 24 or 32-bit samples (n bytes of the stream) to 16-bit big-endian. 24 and 32 bits keep their two
 * most significant bytes; 8 bits are unsigned (centred on 0x80) and double in size, so they are converted from
 * the end (the block was filled with at most half its size, see feed_audio). */
static void to_16bit(uint8_t *d, uint32_t n, int sample_bytes, int big_endian)
{
    uint32_t i, o;

    if (sample_bytes == 1) {
        for (i = n; i-- > 0;) {
            d[2 * i] = (uint8_t)(d[i] ^ 0x80);
            d[2 * i + 1] = 0;
        }
    } else {
        int hi = big_endian ? 0 : sample_bytes - 1, lo = big_endian ? 1 : sample_bytes - 2;

        for (i = 0, o = 0; i + (uint32_t)sample_bytes <= n; i += (uint32_t)sample_bytes, o += 2) {
            uint8_t h = d[i + hi], l = d[i + lo];   /* read before writing: o <= i */

            d[o] = h;
            d[o + 1] = l;
        }
    }
}

/* Bytes of the stream for bytes sent to the device (whole frames): the device gets 16-bit samples */
static uint32_t stream_bytes(const struct audio_block *bl, uint32_t device_bytes)
{
    return device_bytes / 2 * (uint32_t)bl->sample_bytes;
}

/* In place: the stream's samples to 16-bit big-endian, scaled by the volume (16.16, unchanged at full volume),
 * then left and right swapped if asked. Byte by byte, so that the host build gives the same result. */
static void convert(struct audio *a, struct audio_block *bl)
{
    uint8_t *d = bl->buf;
    uint32_t i, n = bl->len;
    int big_endian = bl->big_endian;

    if (bl->sample_bytes != 2) {            /* 16 bits, the usual case, needs no extra pass */
        to_16bit(d, stream_bytes(bl, n), bl->sample_bytes, big_endian);
        big_endian = 1;
    }
    if (a->volume < AUDIO_VOLUME_FULL) {
        int hi = big_endian ? 0 : 1, lo = 1 - hi;

        if (a->tables != a->volume)
            build_tables(a);
        for (i = 0; i + 1 < n; i += 2) {
            int32_t s = (a->high[d[i + hi]] + a->low[d[i + lo]]) >> 16;

            d[i] = (uint8_t)(s >> 8);
            d[i + 1] = (uint8_t)s;
        }
    } else if (!big_endian)
        for (i = 0; i + 1 < n; i += 2) {
            uint8_t t = d[i];
            d[i] = d[i + 1];
            d[i + 1] = t;
        }
    if (a->swap && bl->channels == 2)
        for (i = 0; i + 3 < n; i += 4) {
            uint8_t t0 = d[i], t1 = d[i + 1];
            d[i] = d[i + 2];
            d[i + 1] = d[i + 3];
            d[i + 2] = t0;
            d[i + 3] = t1;
        }
}

/* Sends the ready blocks the device can take, each linked to the one before it (which is playing). A block is
 * converted when it is sent for the first time, at the volume of that moment: a change is heard once the blocks
 * already with the device have played (0.2 to 0.4 s); the rest of a paused block is sent again as it is. */
static void send_ready(struct audio *a, uint32_t now)
{
    int k = sent_count(a);

    while (k < a->count && k < AUDIO_SENT_MAX) {
        int b = a->order[k];
        struct audio_block *bl = &a->block[b];

        if (!bl->converted) {
            convert(a, bl);
            bl->converted = 1;
        }
        bl->sent = bl->len - bl->done;
        bl->state = BLOCK_SENT;
        if (k == 0)
            a->head_ms = now;               /* the device was idle: this block starts now */
        plat_audio_send(b, bl->buf + bl->done, bl->sent, bl->rate, bl->channels, k > 0 ? a->order[k - 1] : -1);
        k++;
    }
}

/* Removes the first k entries of the playing order */
static void drop_first(struct audio *a, int k)
{
    a->count -= k;
    memmove(a->order, a->order + k, (size_t)a->count * sizeof(a->order[0]));
}

void audio_reset(struct player *p)
{
    struct audio *a = &p->audio;
    int k;

    /* From the first one: aborting the block playing also aborts the one linked to it and clears the device's
     * reference to it; aborting the linked one first leaves that reference behind (ahi.device then plays or
     * terminates a request that was given back, and hangs) */
    for (k = 0; k < a->count; k++) {
        struct audio_block *bl = &a->block[a->order[k]];

        if (bl->state == BLOCK_SENT)
            plat_audio_abort(a->order[k]);
        bl->state = BLOCK_FREE;
    }
    a->count = 0;
    a->played = 0;
    a->paused = 0;
}

void audio_poll(struct player *p, uint32_t now)
{
    struct audio *a = &p->audio;
    int ended = 0;

    while (a->count > 0) {
        int b = a->order[0];
        struct audio_block *bl = &a->block[b];

        if (bl->state != BLOCK_SENT || !plat_audio_done(b))
            break;
        a->played += stream_bytes(bl, bl->sent);
        bl->state = BLOCK_FREE;
        drop_first(a, 1);
        a->head_ms = now;                   /* the next block started when this one ended */
        ended = 1;
    }
    if (ended && !a->paused)
        send_ready(a, now);
}

int audio_free_block(const struct player *p)
{
    int i;

    for (i = 0; i < AUDIO_BLOCKS; i++)
        if (p->audio.block[i].state == BLOCK_FREE)
            return i;
    return -1;
}

int audio_pending(const struct player *p)
{
    return p->audio.count;
}

uint8_t *audio_block_data(struct player *p, int b)
{
    return p->audio.block[b].buf;
}

void audio_submit(struct player *p, int b, uint32_t n, const struct pcm_format *f, uint32_t now)
{
    struct audio *a = &p->audio;
    struct audio_block *bl = &a->block[b];

    bl->len = n / f->sample_bytes * 2;      /* what the device will get: 16-bit samples */
    bl->done = 0;
    bl->rate = f->rate;
    bl->channels = f->channels;
    bl->sample_bytes = f->sample_bytes;
    bl->big_endian = f->big_endian;
    bl->converted = 0;                      /* converted when sent, at the volume of that moment */
    bl->state = BLOCK_READY;
    a->order[a->count++] = b;
    if (!a->paused)
        send_ready(a, now);
}

/* Bytes the first block has played by now, estimated from the clock */
static uint32_t head_estimate(const struct audio *a, uint32_t now)
{
    const struct audio_block *bl = &a->block[a->order[0]];
    uint64_t e = (uint64_t)(uint32_t)(now - a->head_ms) * bl->rate * (uint32_t)bl->channels * 2 / 1000;

    return e > bl->sent ? bl->sent : (uint32_t)e;
}

uint64_t audio_played(const struct player *p, uint32_t now, int estimate)
{
    const struct audio *a = &p->audio;

    if (estimate && !a->paused && a->count > 0 && a->block[a->order[0]].state == BLOCK_SENT)
        return a->played + stream_bytes(&a->block[a->order[0]], head_estimate(a, now));
    return a->played;
}

void audio_pause(struct player *p, uint32_t now)
{
    struct audio *a = &p->audio;
    int k, sent, freed = 0;

    if (a->paused)
        return;
    audio_poll(p, now);
    a->paused = 1;
    for (k = 0, sent = sent_count(a); k < sent; k++) {     /* from the first one, see audio_reset() */
        struct audio_block *bl = &a->block[a->order[k]];
        uint32_t frame = (uint32_t)bl->channels * 2, got = 0;
        int32_t actual = plat_audio_abort(a->order[k]);

        if (actual >= 0)
            got = (uint32_t)actual > bl->sent ? bl->sent : (uint32_t)actual;
        else if (k == 0)                    /* the device cannot tell: the clock says */
            got = head_estimate(a, now);
        got -= got % frame;
        bl->done += got;
        a->played += stream_bytes(bl, got);
        bl->state = bl->done >= bl->len ? BLOCK_FREE : BLOCK_READY;
    }
    while (freed < a->count && a->block[a->order[freed]].state == BLOCK_FREE)
        freed++;                            /* blocks that ended just before the pause */
    drop_first(a, freed);
}

void audio_resume(struct player *p, uint32_t now)
{
    struct audio *a = &p->audio;

    if (!a->paused)
        return;
    a->paused = 0;
    send_ready(a, now);
}

/* One volume for both sides (no balance) and no amplification: the louder side, at most full volume. Applied to
 * the blocks sent from now on (see send_ready). */
int audio_set_volume(struct player *p, uint32_t left, uint32_t right, int adjust)
{
    struct audio *a = &p->audio;
    int fixed = adjust == 0;                /* "Output level is fixed at 100%" in LMS */
    uint32_t v = fixed ? AUDIO_VOLUME_FULL : left > right ? left : right;

    if (v > AUDIO_VOLUME_FULL)
        v = AUDIO_VOLUME_FULL;
    if (v == a->volume && fixed == a->fixed)
        return 0;
    a->volume = v;
    a->fixed = fixed;
    return 1;
}
