/*
 * paula.c - portable core: the direct Paula output's arithmetic and conversion
 *
 * Paula plays signed 8-bit samples, one DMA channel each, at a whole period of its clock (PAL or NTSC). 14 bits come
 * from two channels per side: the 8 high bits at volume 64 and the next 6 at volume 1 (uncalibrated). A stream above
 * Paula's highest rate is divided by a whole number, each group of samples played as their mean: a coarse filter,
 * without interpolation. Paula wants even lengths, so a block of an odd count of samples plays its last one twice.
 * audio.c decides which block goes where; the device itself is behind platform.h.
 */
#include <stddef.h>
#include <string.h>

#include "platform.h"
#include "player.h"

uint32_t paula_max_rate(uint32_t clock)
{
    return clock / PAULA_PERIOD_MIN;
}

uint32_t paula_divisor(uint32_t rate, uint32_t clock)
{
    uint32_t max = paula_max_rate(clock);

    if (max == 0 || rate <= max)
        return 1;
    return (rate + max - 1) / max;
}

uint32_t paula_period(uint32_t rate, uint32_t divisor, uint32_t clock)
{
    uint64_t period;

    if (rate == 0)
        return PAULA_PERIOD_MAX;
    period = ((uint64_t)clock * divisor * 2 + rate) / ((uint64_t)rate * 2);     /* clock * divisor / rate, rounded */
    if (period < PAULA_PERIOD_MIN)
        return PAULA_PERIOD_MIN;
    return period > PAULA_PERIOD_MAX ? PAULA_PERIOD_MAX : (uint32_t)period;
}

uint32_t paula_samples(uint32_t frames, uint32_t divisor)
{
    uint32_t n = (frames + divisor - 1) / divisor;

    return n + (n & 1);
}

int paula_volume(uint32_t gain)
{
    uint32_t v;

    if (gain >= AUDIO_VOLUME_FULL)
        return 64;
    if (gain == 0)
        return 0;
    v = (gain * 64 + 0x8000) >> 16;         /* gain < 2^16: no overflow */
    return v > 0 ? (int)v : 1;
}

/* log2(n) if n is a power of two, -1 otherwise */
static int power_of_two(uint32_t n)
{
    int k = 0;

    while (n > 1 && !(n & 1)) {
        n >>= 1;
        k++;
    }
    return n == 1 ? k : -1;
}

/* The mean of channel c's samples in frames [f, f + n), rounded down. Summed with 32768 added to each sample (its
 * sign bit flipped), so that the sum is never negative: a shift or a division of it rounds down, as C's division
 * of a negative number would not. */
static int32_t group_mean(const struct paula_in *in, uint32_t f, uint32_t n, int c, int shift)
{
    uint32_t step = 2 * (uint32_t)in->channels, sum = 0, i;
    const uint8_t *b = in->data + f * step + 2 * (uint32_t)c;
    int hi = in->big_endian ? 0 : 1;

    for (i = 0; i < n; i++, b += step)
        sum += ((uint32_t)b[hi] << 8 | b[1 - hi]) ^ 0x8000;
    if (n == 1)
        return (int32_t)sum - 32768;
    sum = shift >= 0 && n == in->divisor ? sum >> shift : sum / n;
    return (int32_t)sum - 32768;
}

/* Sample k of a side: its 8 high bits (signed), then its next 6 (0 to 63) if the side has a low channel */
static void put(uint8_t *high, uint8_t *low, uint32_t k, int32_t s)
{
    high[k] = (uint8_t)((uint32_t)s >> 8);
    if (low != NULL)
        low[k] = (uint8_t)(((uint32_t)s >> 2) & 0x3F);
}

/* Four bytes a, b, c, d as one 32-bit word holding them in that order in memory, on either byte order: written with
 * memcpy() to an address known aligned, one write (move.l on the Amiga; without the alignment, GCC copies byte by
 * byte). In chip RAM, a write of 4 bytes costs about what a write of 1 does. GCC does not merge four byte writes by
 * itself (seen in its 68020 output). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORD4(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define WORD4(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#else
#error "unknown byte order: define WORD4 for this compiler"
#endif

/* dst: aligned on 4 bytes */
static void put4(uint8_t *dst, uint32_t word)
{
    memcpy(__builtin_assume_aligned(dst, 4), &word, 4);
}

/* convert_whole() for stereo, 4 frames at a time from the first (the buffers start aligned): one write per channel
 * instead of four. Returns the frames done, the last 1 to 3 left to convert_whole(); 0 for mono. */
static uint32_t convert_whole4(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    const uint8_t *h = in->data + (in->big_endian ? 0 : 1), *l = in->data + (in->big_endian ? 1 : 0);
    uint8_t *h0 = high[0], *h1 = high[1], *l0 = low[0], *l1 = low[1];
    uint32_t k;

    if (in->channels != 2)
        return 0;
    for (k = 0; k + 4 <= in->frames; k += 4, h += 16, l += 16) {
        put4(h0 + k, WORD4(h[0], h[4], h[8], h[12]));
        put4(h1 + k, WORD4(h[2], h[6], h[10], h[14]));
        if (l0 != NULL && l1 != NULL) {
            /* each byte shifted right by 2: the bits a shift brings in from the next byte are masked out */
            put4(l0 + k, WORD4(l[0], l[4], l[8], l[12]) >> 2 & 0x3F3F3F3FUL);
            put4(l1 + k, WORD4(l[2], l[6], l[10], l[14]) >> 2 & 0x3F3F3F3FUL);
        }
    }
    return k;
}

/* The rate not divided, the usual case (the server lowers it): each sample's bytes taken as they are, the high one
 * as it is, the low one shifted. The general path cost a slow 68020 more than AHI did (seen in WinUAE). */
static uint32_t convert_whole(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    uint32_t step = 2 * (uint32_t)in->channels, right = in->channels == 2 ? 2 : 0, k = convert_whole4(in, high, low);
    const uint8_t *b = in->data + k * step;
    int hi = in->big_endian ? 0 : 1, lo = 1 - hi;
    uint8_t *h0 = high[0], *h1 = high[1], *l0 = low[0], *l1 = low[1];

    if (l0 != NULL && l1 != NULL) {
        for (; k < in->frames; k++, b += step) {
            h0[k] = b[hi];
            l0[k] = (uint8_t)(b[lo] >> 2);
            h1[k] = b[right + hi];
            l1[k] = (uint8_t)(b[right + lo] >> 2);
        }
    } else {
        for (; k < in->frames; k++, b += step) {
            h0[k] = b[hi];
            h1[k] = b[right + hi];
        }
    }
    return k;
}

/* Sample p of a frame, its sign bit flipped (0 to 65535), as group_mean() sums them */
#define OFFSET_SAMPLE(p, hi, lo) (((uint32_t)(p)[hi] << 8 | (p)[lo]) ^ 0x8000)

/* Sample k of a side from m, a mean with its sign bit flipped: 8 high bits (signed), then the next 6 */
#define PUT_HIGH(dst, k, m) ((dst)[k] = (uint8_t)(((m) >> 8) ^ 0x80))
#define PUT_LOW(dst, k, m)  ((dst)[k] = (uint8_t)(((m) >> 2) & 0x3F))

/* The rate divided by 2 (44.1, 48 and 32 kHz not lowered by the server): each pair's mean, rounded down as
 * group_mean() does, an odd last frame on its own. The general path cost a slow 68020 more than AHI did (seen in
 * WinUAE). */
static uint32_t convert_half(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    const uint8_t *b = in->data;
    uint32_t step = 2 * (uint32_t)in->channels, right = in->channels == 2 ? 2 : 0, k, m0, m1;
    uint32_t pairs = in->frames / 2;
    int hi = in->big_endian ? 0 : 1, lo = 1 - hi;
    uint8_t *h0 = high[0], *h1 = high[1], *l0 = low[0], *l1 = low[1];

    for (k = 0; k < pairs; k++, b += 2 * step) {
        m0 = (OFFSET_SAMPLE(b, hi, lo) + OFFSET_SAMPLE(b + step, hi, lo)) >> 1;
        m1 = (OFFSET_SAMPLE(b + right, hi, lo) + OFFSET_SAMPLE(b + step + right, hi, lo)) >> 1;
        PUT_HIGH(h0, k, m0);
        PUT_HIGH(h1, k, m1);
        if (l0 != NULL) {
            PUT_LOW(l0, k, m0);
            PUT_LOW(l1, k, m1);
        }
    }
    if (in->frames & 1) {
        m0 = OFFSET_SAMPLE(b, hi, lo);
        m1 = OFFSET_SAMPLE(b + right, hi, lo);
        PUT_HIGH(h0, k, m0);
        PUT_HIGH(h1, k, m1);
        if (l0 != NULL) {
            PUT_LOW(l0, k, m0);
            PUT_LOW(l1, k, m1);
        }
        k++;
    }
    return k;
}

uint32_t paula_convert(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    int shift = power_of_two(in->divisor), side;
    uint32_t f, n, k = 0;

    if (in->divisor <= 1)
        k = convert_whole(in, high, low);
    else if (in->divisor == 2)
        k = convert_half(in, high, low);
    else
        for (f = 0; f < in->frames; f += n, k++) {
            n = in->frames - f < in->divisor ? in->frames - f : in->divisor;
            for (side = 0; side < 2; side++)
                put(high[side], low[side], k, group_mean(in, f, n, in->channels == 2 ? side : 0, shift));
        }
    if (k & 1) {                            /* Paula plays an even count */
        for (side = 0; side < 2; side++) {
            high[side][k] = high[side][k - 1];
            if (low[side] != NULL)
                low[side][k] = low[side][k - 1];
        }
        k++;
    }
    return k;
}
