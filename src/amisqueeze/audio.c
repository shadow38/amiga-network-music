/*
 * audio.c - portable core: the blocks of the audio output
 *
 * The platform's audio device plays the blocks one after the other: AHI (plat_ahi_*, ahi.device on the Amiga) or
 * Paula direct (plat_paula_*, audio.device), see platform.h; the device_* functions below choose between them.
 * Here: which block holds what and in which order, the conversion of the stream's PCM for the device (8, 16, 24 or
 * 32-bit samples to 16-bit big-endian, left and right swapped with SWAP), the bytes played (counted in bytes of the
 * stream, whatever the device got), the pause, and the volume. At start-up, the AHI unit's mode (read by the
 * platform layer, from AHI's preferences parsed here) is checked: a mode that would play a stereo stream on one
 * side only, or in mono, gets a warning.
 *
 * AHI: ahi.device chains a request only to one that is already playing: a request linked to a request that is
 * itself waiting is never played (seen in WinUAE, and in AHI's source: the third one is left in the waiting list).
 * So at most two blocks are with the device, the one playing and the next one linked to it; the third block is
 * filled in advance and sent as soon as the first one ends.
 * AHI's CMD_STOP only holds back the requests sent after it (and those of every program): a pause aborts the
 * blocks with the device and sends again, on resume, what they had not played yet.
 * With AHI, the volume is applied to the samples here, as a block is sent for the first time; ahi.device always
 * gets full volume: with ahi.device 6.6 and paula.audio, requests sent again after an abort (a resume) with a low
 * ahir_Volume corrupted the free memory list (seen in WinUAE).
 *
 * Paula direct (audio.device on the Amiga) follows the same order of blocks, two at most with the device. A block is
 * converted when first sent, from its 16-bit samples into the chip RAM buffers of its channels (paula.c); its len,
 * done and sent stay in bytes of those 16-bit samples, so the bytes played are counted as with AHI. The volume is
 * Paula's own, changed at once, never the samples'.
 */
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "player.h"

#define AUDIO_SENT_MAX 2                    /* blocks with the device at the same time */

/* Paula's volumes for the volume set: at full volume (or fixed), 14 bits; below it, the high channels lowered and
 * the low ones silent */
static void paula_set_volume(const struct audio *a)
{
    plat_paula_volume(paula_volume(a->volume), a->volume >= AUDIO_VOLUME_FULL ? 1 : 0);
}

static int device_open(struct audio *a, int unit)
{
    if (!a->paula)
        return plat_ahi_open(unit);
    if (!plat_paula_open(a->paula))
        return 0;
    a->clock = plat_paula_clock();
    paula_set_volume(a);
    return 1;
}

int audio_open(struct player *p, int output, int unit, int swap)
{
    struct audio *a = &p->audio;
    int i;

    memset(a, 0, sizeof(*a));
    a->paula = output == AUDIO_PAULA ? 4 : output == AUDIO_PAULA8 ? 2 : 0;
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
    if (!device_open(a, unit)) {
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
        if (a->paula)
            plat_paula_close();
        else
            plat_ahi_close();
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
 * low[lo], without a multiplication per sample (a 32-bit one costs about 43 cycles on a 68020: about 13 % of a
 * 28 MHz CPU at 44.1 kHz stereo). Built again when the volume changes. */
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

/* AHI, in place: the stream's samples to 16-bit big-endian, scaled by the volume (16.16, unchanged at full volume),
 * then left and right swapped if asked. Byte by byte, so that the host build gives the same result. */
static void convert_ahi(struct audio *a, struct audio_block *bl)
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

/* Paula: bytes of the block's 16-bit samples behind one sample played (a group of divisor frames) */
static uint32_t paula_unit(const struct audio_block *bl)
{
    return 2 * (uint32_t)bl->channels * bl->divisor;
}

/* Paula: block b's 16-bit samples into its channels' buffers, the stream's left to channels 0 and 3 (to 1 and 2
 * with SWAP) */
static void convert_paula(struct audio *a, struct audio_block *bl, int b)
{
    static const int high_channel[2] = { 0, 1 }, low_channel[2] = { 3, 2 };
    struct paula_in in;
    uint8_t *high[2], *low[2];
    int side, x = a->swap ? 1 : 0;

    in.big_endian = bl->big_endian;
    if (bl->sample_bytes != 2) {            /* 16 bits, the usual case, needs no extra pass */
        to_16bit(bl->buf, stream_bytes(bl, bl->len), bl->sample_bytes, bl->big_endian);
        in.big_endian = 1;
    }
    for (side = 0; side < 2; side++) {
        high[side] = plat_paula_buffer(b, high_channel[side ^ x]);
        low[side] = a->paula == 4 ? plat_paula_buffer(b, low_channel[side ^ x]) : NULL;
    }
    in.data = bl->buf;
    in.frames = bl->len / (2 * (uint32_t)bl->channels);
    in.channels = bl->channels;
    in.divisor = bl->divisor;
    paula_convert(&in, high, low);
}

/* Sends block b, k-th of the playing order: with AHI linked to the one before it (which is playing); with Paula,
 * the samples from the one done is at (even, see audio_pause) */
static void device_send(struct audio *a, int b, int k)
{
    struct audio_block *bl = &a->block[b];
    uint32_t start;

    if (!a->paula) {
        plat_ahi_send(b, bl->buf + bl->done, bl->sent, bl->rate, bl->channels, k > 0 ? a->order[k - 1] : -1);
        return;
    }
    start = bl->done / paula_unit(bl);
    plat_paula_send(b, start, paula_samples(bl->len / (2 * (uint32_t)bl->channels), bl->divisor) - start,
                    bl->period);
}

static int device_done(const struct audio *a, int b)
{
    return a->paula ? plat_paula_done(b) : plat_ahi_done(b);
}

/* Aborts block b; returns the bytes of its 16-bit samples played (Paula: each sample played counts for its group,
 * the last one maybe more than the block holds), or -1 if the device cannot tell */
static int32_t device_abort(const struct audio *a, int b)
{
    int32_t n;

    if (!a->paula)
        return plat_ahi_abort(b);
    n = plat_paula_abort(b);
    return n < 0 ? -1 : n * (int32_t)paula_unit(&a->block[b]);
}

/* Sends the ready blocks the device can take. A block is converted when it is sent for the first time, at the
 * volume of that moment (AHI): a change is heard once the blocks already with the device have played (0.2 to
 * 0.4 s); the rest of a paused block is sent again as it is. */
static void send_ready(struct audio *a, uint32_t now)
{
    int k = sent_count(a);

    while (k < a->count && k < AUDIO_SENT_MAX) {
        int b = a->order[k];
        struct audio_block *bl = &a->block[b];

        if (!bl->converted) {
            if (a->paula)
                convert_paula(a, bl, b);
            else
                convert_ahi(a, bl);
            bl->converted = 1;
        }
        bl->sent = bl->len - bl->done;
        bl->state = BLOCK_SENT;
        if (k == 0)
            a->head_ms = now;               /* the device was idle: this block starts now */
        device_send(a, b, k);
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
            device_abort(a, a->order[k]);
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

        if (bl->state != BLOCK_SENT || !device_done(a, b))
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

/* 8-bit samples fill half a block: they double in size when converted to 16 bits. With Paula, a block holds whole
 * groups of two samples played (even lengths), so that only a track's last block plays a sample twice or a group
 * cut short. */
uint32_t audio_block_room(const struct player *p, const struct pcm_format *f)
{
    uint32_t frame = (uint32_t)f->channels * f->sample_bytes, unit = frame;
    uint32_t room = f->sample_bytes == 1 ? AUDIO_BLOCK_SIZE / 2 : AUDIO_BLOCK_SIZE;

    if (p->audio.paula) {
        uint32_t divisor = paula_divisor(f->rate, p->audio.clock);

        unit = 2 * divisor * frame;
        if (room > PAULA_BLOCK_SAMPLES * divisor * frame)
            room = PAULA_BLOCK_SAMPLES * divisor * frame;
    }
    return room - room % unit;
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
    if (a->paula) {
        bl->divisor = paula_divisor(f->rate, a->clock);
        bl->period = paula_period(f->rate, bl->divisor, a->clock);
    }
    bl->state = BLOCK_READY;
    a->order[a->count++] = b;
    if (!a->paused)
        send_ready(a, now);
}

/* Bytes the first block has played by now, estimated from the clock; with Paula, at the rate its period gives */
static uint32_t head_estimate(const struct audio *a, uint32_t now)
{
    const struct audio_block *bl = &a->block[a->order[0]];
    uint32_t ms = now - a->head_ms;
    uint64_t e;

    if (a->paula)
        e = (uint64_t)ms * a->clock / ((uint64_t)bl->period * 1000) * paula_unit(bl);
    else
        e = (uint64_t)ms * bl->rate * (uint32_t)bl->channels * 2 / 1000;
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
        /* the rest is sent again from a whole frame; with Paula, from an even sample (an even length) */
        uint32_t unit = a->paula ? 2 * paula_unit(bl) : (uint32_t)bl->channels * 2, got = 0;
        int32_t actual = device_abort(a, a->order[k]);

        if (actual >= 0)
            got = (uint32_t)actual > bl->sent ? bl->sent : (uint32_t)actual;
        else if (k == 0)                    /* the device cannot tell: the clock says */
            got = head_estimate(a, now);
        if (got < bl->sent)
            got -= got % unit;
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
 * the blocks sent from now on (see send_ready), or with Paula to its channels at once. */
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
    if (a->enabled && a->paula)
        paula_set_volume(a);
    return 1;
}

static uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

/* An AHIU chunk is a struct AHIUnitPrefs (devices/ahi.h): unit (1 byte), 1 byte, channels (2), audio mode ID (4),
 * frequency (4), then levels and inputs */
int ahi_prefs_unit(const uint8_t *prefs, uint32_t size, int unit, uint32_t *mode, uint32_t *freq)
{
    uint32_t pos = 12, end;

    if (size < 12 || memcmp(prefs, "FORM", 4) || memcmp(prefs + 8, "PREF", 4))
        return 0;
    end = be32(prefs + 4) + 8;
    if (end > size)
        end = size;
    while (pos + 8 <= end) {
        uint32_t len = be32(prefs + pos + 4);

        if (len > end - pos - 8)
            break;                              /* a chunk cut short: the rest is not trusted */
        if (!memcmp(prefs + pos, "AHIU", 4) && len >= 12 && prefs[pos + 8] == unit) {
            *mode = be32(prefs + pos + 12);
            *freq = be32(prefs + pos + 16);
            return 1;
        }
        pos += 8 + len + (len & 1);             /* chunks are padded to an even size */
    }
    return 0;
}

int ahi_check_mode(int unit, const struct ahi_mode *m)
{
    if (m->stereo && !m->panning) {
        msg("amisqueeze: AHI unit %d uses \"%s\", a stereo mode without \"++\": the music will play on one side "
            "only; choose a \"stereo++\" mode in SYS:Prefs/AHI\n", unit, m->name);
        return 1;
    }
    if (!m->stereo) {
        msg("amisqueeze: AHI unit %d uses \"%s\", a mono mode: the music will play in mono\n", unit, m->name);
        return 1;
    }
    return 0;
}
