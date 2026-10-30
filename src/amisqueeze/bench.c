/*
 * bench.c - portable core: how fast this machine decodes FLAC and MP3. A built-in
 * sample is decoded again and again, the PCM thrown away: for FLAC bench_flac.c, made from
 * tests/data/bench-l8.flac (0.3 s of the test signal at level 8, the slowest level to decode); for MP3
 * bench_mp3.c, made from tests/data/bench-320.mp3 (the same 0.3 s at 320 kbit/s, the highest rate). The FLAC and
 * MP3 options decide on those speeds when AUTO; the BENCH option prints them.
 */
#include <string.h>

#include "decoder.h"
#include "platform.h"
#include "player.h"

extern const uint8_t bench_flac[];
extern const uint32_t bench_flac_size;
#ifdef AMISQUEEZE_MP3
extern const uint8_t bench_mp3[];
extern const uint32_t bench_mp3_size;
#endif

#define BENCH_MS 300                        /* measure for about that long, at least one whole pass if quicker */

/* Speed of a decoder on a sample, in % of real time; 0 if it cannot be measured */
static uint32_t decode_speed(const struct decoder_ops *ops, const uint8_t *sample, uint32_t size, uint32_t max_ms)
{
    static uint16_t pcm[2048];
    struct ring r;
    struct decoder d;
    struct pcm_format in = { 44100, 2, 2, 0 };
    uint64_t frames = 0;
    uint32_t start, elapsed = 0, rate = 0;

    if (!ring_init(&r, size))
        return 0;
    in.big_endian = (uint8_t)host_big_endian();
    start = plat_now_ms();
    while (elapsed < BENCH_MS && elapsed < max_ms) {
        uint8_t *w;

        ring_clear(&r);
        ring_write_space(&r, &w);
        memcpy(w, sample, size);
        ring_commit(&r, size);
        memset(&d, 0, sizeof(d));
        d.ops = ops;
        if (!d.ops->start(&d, &in))
            break;
        for (;;) {
            uint32_t got = d.ops->run(&d, &r, ring_used(&r), 1, (uint8_t *)pcm, sizeof(pcm));

            elapsed = plat_now_ms() - start;
            if (got == 0)
                break;
            frames += got / (2U * d.out.channels);
            if (elapsed >= max_ms)
                break;                      /* far too slow: no need to finish the pass */
        }
        rate = d.out.rate;
        d.ops->stop(&d);
    }
    ring_free(&r);
    if (elapsed == 0 || rate == 0)
        return 0;
    return (uint32_t)(frames * 100000 / rate / elapsed);
}

uint32_t flac_speed(uint32_t max_ms)
{
    return decode_speed(&decoder_flac, bench_flac, bench_flac_size, max_ms);
}

#ifdef AMISQUEEZE_MP3
uint32_t mp3_speed(uint32_t max_ms)
{
    return decode_speed(&decoder_mp3, bench_mp3, bench_mp3_size, max_ms);
}
#endif
