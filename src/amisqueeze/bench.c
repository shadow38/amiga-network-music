/*
 * bench.c - portable core: how fast this machine decodes FLAC (plan step 1.8b). A built-in sample (bench_flac.c,
 * made from tests/data/bench-l8.flac: 0.3 s of the test signal at level 8, the slowest level to decode) is decoded
 * again and again, the PCM thrown away. FLAC=AUTO decides on that speed; the BENCH option prints it.
 */
#include <string.h>

#include "decoder.h"
#include "platform.h"
#include "player.h"

extern const uint8_t bench_flac[];
extern const uint32_t bench_flac_size;

#define BENCH_MS 300                        /* measure for about that long, at least one whole pass if quicker */

uint32_t flac_speed(uint32_t max_ms)
{
    static uint16_t pcm[2048];
    struct ring r;
    struct decoder d;
    struct pcm_format in = { 44100, 2, 2, 0 };
    uint64_t frames = 0;
    uint32_t start, elapsed = 0, rate = 0;

    if (!ring_init(&r, bench_flac_size))
        return 0;
    in.big_endian = (uint8_t)host_big_endian();
    start = plat_now_ms();
    while (elapsed < BENCH_MS && elapsed < max_ms) {
        uint8_t *w;

        ring_clear(&r);
        ring_write_space(&r, &w);
        memcpy(w, bench_flac, bench_flac_size);
        ring_commit(&r, bench_flac_size);
        memset(&d, 0, sizeof(d));
        d.ops = &decoder_flac;
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
