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

/* The rate not divided, the usual case (the server lowers it): each sample's bytes taken as they are, the high one
 * as it is, the low one shifted. The general path cost a slow 68020 more than AHI did (seen in WinUAE). */
static uint32_t convert_whole(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    const uint8_t *b = in->data;
    uint32_t step = 2 * (uint32_t)in->channels, right = in->channels == 2 ? 2 : 0, k;
    int hi = in->big_endian ? 0 : 1, lo = 1 - hi;
    uint8_t *h0 = high[0], *h1 = high[1], *l0 = low[0], *l1 = low[1];

    if (l0 != NULL && l1 != NULL) {
        for (k = 0; k < in->frames; k++, b += step) {
            h0[k] = b[hi];
            l0[k] = (uint8_t)(b[lo] >> 2);
            h1[k] = b[right + hi];
            l1[k] = (uint8_t)(b[right + lo] >> 2);
        }
    } else {
        for (k = 0; k < in->frames; k++, b += step) {
            h0[k] = b[hi];
            h1[k] = b[right + hi];
        }
    }
    return k;
}

uint32_t paula_convert(const struct paula_in *in, uint8_t *const high[2], uint8_t *const low[2])
{
    int shift = power_of_two(in->divisor), side;
    uint32_t f, n, k = 0;

    if (in->divisor <= 1)
        k = convert_whole(in, high, low);
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
