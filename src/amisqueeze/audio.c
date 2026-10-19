/*
 * audio.c - portable core: the blocks of the audio output (plan step 1.4b)
 *
 * The platform's audio device (ahi.device on the Amiga, see platform.h) plays the blocks one after the other.
 * Here: which block holds what and in which order, the conversion of the stream's PCM for the device (16-bit
 * big-endian, left and right swapped with SWAP), the bytes played, the pause, and the volume.
 *
 * ahi.device chains a request only to one that is already playing: a request linked to a request that is itself
 * waiting is never played (seen in WinUAE, and in AHI's source: the third one is left in the waiting list). So at
 * most two blocks are with the device, the one playing and the next one linked to it; the third block is filled
 * in advance and sent as soon as the first one ends.
 * AHI's CMD_STOP only holds back the requests sent after it (and those of every program): a pause aborts the
 * blocks with the device and sends again, on resume, what they had not played yet.
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

/* Sends the ready blocks the device can take, each linked to the one before it (which is playing) */
static void send_ready(struct audio *a, uint32_t now)
{
    int k = sent_count(a);

    while (k < a->count && k < AUDIO_SENT_MAX) {
        int b = a->order[k];
        struct audio_block *bl = &a->block[b];

        bl->sent = bl->len - bl->done;
        bl->state = BLOCK_SENT;
        if (k == 0)
            a->head_ms = now;               /* the device was idle: this block starts now */
        plat_audio_send(b, bl->buf + bl->done, bl->sent, bl->rate, bl->channels, k > 0 ? a->order[k - 1] : -1,
                        a->volume);
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
        a->played += bl->sent;
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

/* In place: 16-bit samples to big-endian, then left and right swapped if asked */
static void convert(uint8_t *d, uint32_t n, const struct pcm_format *f, int swap)
{
    uint32_t i;

    if (!f->big_endian)
        for (i = 0; i + 1 < n; i += 2) {
            uint8_t t = d[i];
            d[i] = d[i + 1];
            d[i + 1] = t;
        }
    if (swap && f->channels == 2)
        for (i = 0; i + 3 < n; i += 4) {
            uint8_t t0 = d[i], t1 = d[i + 1];
            d[i] = d[i + 2];
            d[i + 1] = d[i + 3];
            d[i + 2] = t0;
            d[i + 3] = t1;
        }
}

void audio_submit(struct player *p, int b, uint32_t n, const struct pcm_format *f, uint32_t now)
{
    struct audio *a = &p->audio;
    struct audio_block *bl = &a->block[b];
    uint32_t got = 0;

    while (got < n) {                       /* the ring buffer may wrap around: two copies */
        const uint8_t *d;
        uint32_t len = ring_read_space(&p->ring, &d);

        if (len > n - got)
            len = n - got;
        memcpy(bl->buf + got, d, len);
        ring_consume(&p->ring, len);
        got += len;
    }
    convert(bl->buf, n, f, a->swap);
    bl->len = n;
    bl->done = 0;
    bl->rate = f->rate;
    bl->channels = f->channels;
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
        return a->played + head_estimate(a, now);
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
        a->played += got;
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

/* The device takes one volume per request and cannot amplify: the louder side, at most full volume. Blocks
 * already with the device keep theirs, so a change is heard 0.2 to 0.4 s later, at no CPU cost (scaling the
 * samples here would act at once, but cost about a tenth of a 28 MHz 68020). */
int audio_set_volume(struct player *p, uint32_t left, uint32_t right, int adjust)
{
    struct audio *a = &p->audio;
    uint32_t v = !adjust ? AUDIO_VOLUME_FULL : left > right ? left : right;

    if (v > AUDIO_VOLUME_FULL)
        v = AUDIO_VOLUME_FULL;
    if (v == a->volume)
        return 0;
    a->volume = v;
    return 1;
}
