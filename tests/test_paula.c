/*
 * test_paula.c - unit tests of paula.c and of audio.c with the direct Paula output (host build, see "make test"):
 * the rate divided, the period, the volume, the conversion to Paula's channels bit for bit, the bytes played
 */
#include "test_core.h"

#define PAL  3546895UL
#define NTSC 3579545UL

static uint8_t ref[4][STUB_PAULA_OUT];      /* what each channel must play */
static _Alignas(4) uint8_t pcm16[2 * PCM_BPS];  /* aligned as malloc() aligns the samples' buffers */

/* ---- references, made here without paula.c's tricks ---- */

static int32_t sample16(const uint8_t *b, int big_endian)
{
    return (int16_t)(big_endian ? b[0] << 8 | b[1] : b[1] << 8 | b[0]);
}

/* sum / n rounded down, negative sums included */
static int32_t mean_down(int32_t sum, int32_t n)
{
    int32_t q = sum / n;

    return q * n > sum ? q - 1 : q;
}

/* Paula's two bytes for the 16-bit sample s: the high byte, signed, and the next 6 bits, from 0 to 63 */
static uint8_t high_of(int32_t s)
{
    return (uint8_t)((s + 32768) / 256 + 128);
}

static uint8_t low_of(int32_t s)
{
    return (uint8_t)((s + 32768) / 4 % 64);
}

/* What channels 0 to 3 must play for 16-bit PCM: per side, the mean of each group of d frames (the last one maybe
 * shorter), the stream's left on channels 0 and 3 (1 and 2 with swap), mono on both sides, an odd count made even
 * with its last sample again. Returns the samples per channel. */
static uint32_t ref_paula(const uint8_t *src, uint32_t frames, int channels, int big_endian, uint32_t d, int swap)
{
    static const int high_channel[2] = { 0, 1 }, low_channel[2] = { 3, 2 };
    uint32_t f, k = 0;
    int c;

    for (f = 0; f < frames; f += d, k++) {
        uint32_t n = frames - f < d ? frames - f : d, i;
        int side;

        for (side = 0; side < 2; side++) {
            int32_t sum = 0, s;

            for (i = 0; i < n; i++)
                sum += sample16(src + ((f + i) * (uint32_t)channels + (channels == 2 ? (uint32_t)side : 0)) * 2,
                                big_endian);
            s = mean_down(sum, (int32_t)n);
            ref[high_channel[side ^ swap]][k] = high_of(s);
            ref[low_channel[side ^ swap]][k] = low_of(s);
        }
    }
    if (k & 1) {
        for (c = 0; c < 4; c++)
            ref[c][k] = ref[c][k - 1];
        k++;
    }
    return k;
}

/* 1 if the first `channels` channels played what ref holds, n samples each */
static int played_ref(int channels, uint32_t n)
{
    int c;

    if (stub_paula_out_len != n)
        return 0;
    for (c = 0; c < channels; c++)
        if (memcmp(stub_paula_out[c], ref[c], n))
            return 0;
    return 1;
}

/* ---- paula.c alone ---- */

/* The highest rate and the divisor (max -> 1, max + 1 -> 2); then the period rounded, the samples played */
static void test_paula_rates(void)
{
    CHECK(paula_max_rate(PAL) == 28603 && paula_max_rate(NTSC) == 28867);
    CHECK(paula_divisor(28603, PAL) == 1 && paula_divisor(28604, PAL) == 2);
    CHECK(paula_divisor(28867, NTSC) == 1 && paula_divisor(28868, NTSC) == 2);
    CHECK(paula_divisor(22050, PAL) == 1 && paula_divisor(0, PAL) == 1);
    CHECK(paula_divisor(44100, PAL) == 2 && paula_divisor(48000, PAL) == 2 && paula_divisor(32000, PAL) == 2);
    CHECK(paula_divisor(88200, PAL) == 4 && paula_divisor(96000, PAL) == 4);
    CHECK(paula_divisor(176400, PAL) == 7 && paula_divisor(192000, PAL) == 7);
}

static void test_paula_period(void)
{
    CHECK(paula_period(22050, 1, PAL) == 161 && paula_period(22050, 1, NTSC) == 162);   /* 160.86 and 162.34 */
    CHECK(paula_period(44100, 2, PAL) == 161 && paula_period(48000, 2, PAL) == 148);     /* 160.86 and 147.79 */
    CHECK(paula_period(44100, 3, PAL) == 241);          /* 241.29: from clock * 3 / 44100, not from 44100 / 3 */
    CHECK(paula_period(28603, 1, PAL) == PAULA_PERIOD_MIN);
    CHECK(paula_period(40, 1, PAL) == PAULA_PERIOD_MAX && paula_period(0, 1, PAL) == PAULA_PERIOD_MAX);

    CHECK(paula_samples(0, 1) == 0 && paula_samples(5, 1) == 6 && paula_samples(6, 1) == 6);
    CHECK(paula_samples(7, 2) == 4 && paula_samples(8, 2) == 4 && paula_samples(9, 2) == 6);
    CHECK(paula_samples(15, 7) == 4);
}

/* The server's 16.16 gain to Paula's 0 to 64: 0 only for 0, 64 at full or above */
static void test_paula_volume_map(void)
{
    CHECK(paula_volume(0) == 0 && paula_volume(1) == 1 && paula_volume(0x200) == 1);
    CHECK(paula_volume(0x8000) == 32 && paula_volume(0x4000) == 16);
    CHECK(paula_volume(0xFFFF) == 64 && paula_volume(0x10000) == 64 && paula_volume(0x20000) == 64);
}

/* Extreme and negative samples first, then pseudo-random ones, as 16-bit PCM in the byte order given */
static void make_pcm(uint8_t *dst, uint32_t samples, int big_endian, uint32_t seed)
{
    static const int32_t edge[] = { -32768, -1, 0, 32767, -2, -1, -3, 5, -256, 255, 256, -257, 12345, -12345 };
    uint32_t i;

    fill(dst, samples * 2, seed);
    for (i = 0; i < samples && i < sizeof(edge) / sizeof(edge[0]); i++) {
        uint16_t v = (uint16_t)edge[i];

        dst[2 * i + (big_endian ? 0 : 1)] = (uint8_t)(v >> 8);
        dst[2 * i + (big_endian ? 1 : 0)] = (uint8_t)v;
    }
}

/* paula_convert() against the reference, for 14 then 8 bits (the low channels left alone) */
static void check_convert(int channels, int big_endian, uint32_t d, uint32_t frames, uint32_t seed)
{
    static _Alignas(4) uint8_t got[4][1024];         /* aligned on 4 bytes, as Paula's buffers */
    struct paula_in in;
    uint8_t *high[2], *low[2];
    uint32_t n, expect_n;
    int ch;

    make_pcm(pcm16, frames * (uint32_t)channels, big_endian, seed);
    in.data = pcm16;
    in.frames = frames;
    in.channels = channels;
    in.big_endian = big_endian;
    in.divisor = d;
    memset(got, 0xEE, sizeof(got));
    high[0] = got[0], high[1] = got[1], low[0] = got[3], low[1] = got[2];
    n = paula_convert(&in, high, low);
    expect_n = ref_paula(pcm16, in.frames, in.channels, in.big_endian, in.divisor, 0);
    CHECK(n == expect_n && n == paula_samples(in.frames, in.divisor));
    for (ch = 0; ch < 4; ch++)
        CHECK(!memcmp(got[ch], ref[ch], n) && got[ch][n] == 0xEE);

    memset(got, 0xEE, sizeof(got));                 /* 8 bits */
    low[0] = low[1] = NULL;
    CHECK(paula_convert(&in, high, low) == n);
    CHECK(!memcmp(got[0], ref[0], n) && !memcmp(got[1], ref[1], n));
    CHECK(got[2][0] == 0xEE && got[3][0] == 0xEE);
}

/* Divisors 1 to 7 (powers of two and not), mono and stereo, both byte orders, a last group cut short, an odd
 * count. Not divided, stereo goes 4 frames at a time: 1 to 9 frames, so 0 to 3 left after the groups, in both byte
 * orders. Divided by 2, stereo goes 8 frames at a time: 0 to 17 frames, so every remainder and an odd last frame,
 * in both byte orders; mono beside it. */
static void test_paula_convert(void)
{
    static const struct { int channels, big_endian; uint32_t d, frames; } cases[] = {
        { 2, 0, 1, 10 }, { 2, 1, 2, 9 }, { 1, 0, 3, 10 }, { 2, 0, 7, 15 }, { 1, 1, 2, 7 }, { 2, 0, 4, 64 },
        { 2, 0, 3, 1000 }, { 2, 1, 2, 0 },
        { 2, 0, 1, 1 }, { 2, 1, 1, 2 }, { 2, 0, 1, 3 }, { 2, 1, 1, 4 }, { 2, 0, 1, 5 }, { 2, 1, 1, 6 },
        { 2, 0, 1, 7 }, { 2, 1, 1, 8 }, { 2, 0, 1, 9 }, { 2, 1, 1, 1000 }, { 1, 0, 1, 9 }, { 1, 1, 1, 8 },
        { 2, 0, 2, 1000 }, { 2, 1, 2, 1000 }, { 2, 0, 2, 1001 }, { 1, 0, 2, 17 }, { 1, 1, 2, 16 },
    };
    uint32_t c, f;
    int be;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        check_convert(cases[c].channels, cases[c].big_endian, cases[c].d, cases[c].frames, c + 1);
    for (be = 0; be < 2; be++)
        for (f = 0; f <= 17; f++)
            check_convert(2, be, 2, f, 100 + f);
}

/* Known values: -1 and -2 make -2 (rounded down), Paula's bytes 0xFF and 63; -32768 and 32767 at both ends */
static void test_paula_convert_values(void)
{
    static const uint8_t le[] = { 0xFF, 0xFF, 0x00, 0x80, 0xFE, 0xFF, 0xFF, 0x7F };   /* L -1, R -32768, L -2, R 32767 */
    struct paula_in in = { le, 2, 2, 0, 1 };
    uint8_t h0[4], h1[4], l0[4], l1[4];
    uint8_t *high[2] = { h0, h1 }, *low[2] = { l0, l1 };

    CHECK(paula_convert(&in, high, low) == 2);
    CHECK(h0[0] == 0xFF && l0[0] == 63 && h0[1] == 0xFF && l0[1] == 63);   /* -1 >> 2 and -2 >> 2: -1 */
    CHECK(h1[0] == 0x80 && l1[0] == 0 && h1[1] == 0x7F && l1[1] == 63);
    in.divisor = 2;
    CHECK(paula_convert(&in, high, low) == 2);
    CHECK(h0[0] == 0xFF && l0[0] == 63 && h0[1] == 0xFF);     /* (-1 - 2) / 2 = -1.5: -2 */
    CHECK(h1[0] == 0xFF && l1[0] == 63);                      /* (-32768 + 32767) / 2 = -0.5: -1 */
}

/* ---- audio.c with Paula ---- */

static void setup_paula(struct player *p, int output, int swap)
{
    setup(p, "Amiga");
    stub_paula_fails = 0;
    stub_paula_abort_played = 0;
    CHECK(audio_open(p, output, 0, swap) && stub_paula_open == (output == AUDIO_PAULA ? 4 : 2));
    stub_paula_clear();
    CHECK(slim_connect(p, 0));
}

/* strm s and the response headers for PCM of the given size, rate, channels and byte order (slimproto's codes) */
static void open_paula_stream(struct player *p, char size, char rate, char channels, char endian)
{
    char fmt[7] = { '1', 'p', size, rate, channels, endian, 1 };

    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(p);
    stream_io(p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(p);
}

/* Ends the block playing now, then lets the output collect it and refill */
static int paula_finish(struct player *p)
{
    int b = stub_paula_finish();

    stub_now += 10;
    output_tick(p, stub_now);
    return b;
}

static void paula_play_out(struct player *p)
{
    int guard;

    stub_stream_end();
    stream_io(p, PLAT_READ);
    for (guard = 0; guard < 40 && p->out.state == OUTPUT_RUNNING; guard++)
        paula_finish(p);
}

/* 44.1 kHz stereo on PAL: divided by 2, period 161 (22,030 Hz). Blocks of 32 KB, two with the device; the elapsed
 * time estimated at the rate the period gives (AHI's estimate would say 100 ms); the last block ends on a group cut
 * short and an odd count; everything played is the reference, and the bytes played reach the stream's at the end */
static void test_paula_play(void)
{
    struct player p;
    uint32_t n = sizeof(track_a) - 12, samples;
    int order[8];

    fill(track_a, sizeof(track_a), 11);
    setup_paula(&p, AUDIO_PAULA, 0);
    CHECK(stub_paula_high == 64 && stub_paula_low == 1);    /* full volume: 14 bits */
    open_paula_stream(&p, '1', '3', '2', '1');
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_paula_queue(order) == 2 && stub_paula_sends == 2);
    CHECK(stub_paula[order[0]].start == 0 && stub_paula[order[0]].len == BLOCK / 8);
    CHECK(stub_paula[order[0]].period == 161 && audio_pending(&p) == AUDIO_BLOCKS);
    stub_now += 100;
    CHECK(output_elapsed_ms(&p) == 99);     /* 100 * 3546895 / 161000 = 2203 samples, 17624 bytes */

    paula_play_out(&p);
    samples = ref_paula(track_a, n / 4, 2, 0, 2, 0);
    CHECK(samples == 5 * BLOCK / 8 + 1570);         /* 3137 frames left: 1569 groups, one more for Paula */
    CHECK(played_ref(4, samples));
    CHECK(p.out.state == OUTPUT_STOPPED && audio_played(&p, stub_now, 0) == n);
    CHECK(stub_paula_bad == 0 && stub_paula_sends == 6);
    teardown(&p);
}

/* A pause in the middle of a block, divided by 2: an odd count played is taken back to an even one (Paula's even
 * start), the bytes played follow it; resumed from there, the music follows on. Then a device that cannot tell:
 * the clock says. */
static void test_paula_pause(void)
{
    struct player p;
    uint32_t samples;
    int order[8];

    fill(track_a, sizeof(track_a), 12);
    setup_paula(&p, AUDIO_PAULA, 0);
    open_paula_stream(&p, '1', '3', '2', '1');
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_paula_abort_played = 1001;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_paula_queue(order) == 0);
    CHECK(audio_played(&p, stub_now, 1) == 1000 * 8);
    CHECK(output_elapsed_ms(&p) == 1000 * 8 * 1000 / PCM_BPS);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_paula_queue(order) == 2);
    CHECK(stub_paula[order[0]].start == 1000 && stub_paula[order[0]].len == BLOCK / 8 - 1000);
    CHECK(stub_paula[order[1]].start == 0 && stub_paula[order[1]].len == BLOCK / 8);

    stub_paula_abort_played = -1;           /* 50 ms: 1101 samples, so from 1100 */
    stub_now += 50;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(audio_played(&p, stub_now, 1) == 2100 * 8);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_paula_queue(order) == 2 && stub_paula[order[0]].start == 2100);

    paula_play_out(&p);
    samples = ref_paula(track_a, sizeof(track_a) / 4, 2, 0, 2, 0);
    /* captured: samples 0 to 1000 at the first pause, nothing at the second, then 2100 to the end */
    CHECK(stub_paula_out_len == 1001 + samples - 2100);
    CHECK(!memcmp(stub_paula_out[0], ref[0], 1001) && !memcmp(stub_paula_out[3], ref[3], 1001));
    CHECK(!memcmp(stub_paula_out[0] + 1001, ref[0] + 2100, samples - 2100));
    CHECK(!memcmp(stub_paula_out[2] + 1001, ref[2] + 2100, samples - 2100));
    CHECK(p.out.state == OUTPUT_STOPPED && audio_played(&p, stub_now, 0) == sizeof(track_a));
    CHECK(stub_paula_bad == 0);
    teardown(&p);
}

/* 16-bit mono at 22.05 kHz, not divided: a block holds 8192 samples per channel, not 16384 (Paula's buffers); NTSC
 * gives its own period and the same elapsed time */
static void test_paula_mono_ntsc(void)
{
    struct player p;
    uint32_t n = sizeof(track_a) / 2, samples;
    int order[8];

    fill(track_a, sizeof(track_a), 13);
    stub_paula_clock = NTSC;
    setup_paula(&p, AUDIO_PAULA, 1);        /* SWAP changes nothing to mono */
    open_paula_stream(&p, '1', '1', '1', '1');
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_paula_queue(order) == 2 && stub_paula[order[0]].len == PAULA_BLOCK_SAMPLES);
    CHECK(stub_paula[order[0]].period == 162);
    stub_now += 100;
    CHECK(output_elapsed_ms(&p) == 100);    /* 100 * 3579545 / 162000 = 2209 samples, 4418 bytes */
    paula_play_out(&p);
    samples = ref_paula(track_a, n / 2, 1, 0, 1, 0);
    CHECK(played_ref(4, samples) && audio_played(&p, stub_now, 0) == n);
    CHECK(stub_paula_bad == 0);
    teardown(&p);
    stub_paula_clock = PAL;
}

/* 8 bits with SWAP: channels 0 and 1 only, the stream's left on the right; 24-bit samples at 48 kHz, divided by 2
 * after their two high bytes are kept; 8-bit samples, unsigned */
static void test_paula_formats(void)
{
    static const struct { char size, rate; int bytes, d; uint32_t rate_hz; } cases[] = {
        { '2', '4', 3, 2, 48000 },
        { '0', '3', 1, 2, 44100 },
        { '1', '2', 2, 2, 32000 },
    };
    uint32_t c;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        struct player p;
        uint32_t frame = 2 * (uint32_t)cases[c].bytes, n = 100000 - 100000 % frame, samples, dev;

        fill(track_a, sizeof(track_a), 14 + c);
        setup_paula(&p, AUDIO_PAULA8, 1);
        open_paula_stream(&p, cases[c].size, cases[c].rate, '2', '1');
        stub_feed_stream(track_a, (int)n);
        drain_stream(&p);
        output_tick(&p, stub_now);
        paula_play_out(&p);
        dev = to_device_fmt(pcm16, track_a, n, cases[c].bytes, 0);
        samples = ref_paula(pcm16, dev / 4, 2, 1, (uint32_t)cases[c].d, 1);
        CHECK(played_ref(2, samples));
        CHECK(stub_paula[0].period == paula_period(cases[c].rate_hz, (uint32_t)cases[c].d, PAL));
        CHECK(audio_played(&p, stub_now, 0) == n && p.out.state == OUTPUT_STOPPED);
        CHECK(stub_paula_bad == 0);                 /* channels 2 and 3 never asked for */
        teardown(&p);
    }
}

/* strm q while two blocks are with the device: the one playing aborted first, nothing left queued; strm f while the
 * only track plays: what it received plays to its end. Closed, no channel left open */
static void test_paula_stop(void)
{
    struct player p;
    uint32_t n = sizeof(track_a) / 2;
    int order[8], guard;

    fill(track_a, sizeof(track_a), 16);
    setup_paula(&p, AUDIO_PAULA, 0);
    open_paula_stream(&p, '1', '3', '2', '1');
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_paula_queue(order) == 2);
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_paula_queue(order) == 0 && audio_pending(&p) == 0 && p.out.state == OUTPUT_STOPPED);
    CHECK(stub_paula_bad == 0);
    audio_close(&p);
    CHECK(stub_paula_open == 0);
    teardown(&p);

    setup_paula(&p, AUDIO_PAULA, 0);
    open_paula_stream(&p, '1', '3', '2', '1');
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(p.out.state == OUTPUT_RUNNING && stub_paula_queue(order) == 2);
    for (guard = 0; guard < 40 && p.out.state == OUTPUT_RUNNING; guard++)
        paula_finish(&p);
    CHECK(p.out.state == OUTPUT_STOPPED && audio_played(&p, stub_now, 0) == n);
    CHECK(stub_paula_bad == 0);
    audio_close(&p);
    CHECK(stub_paula_open == 0);
    teardown(&p);
}

/* A file of tests/data/ (make test runs from the repository's root) */
static uint32_t data_file(const char *name, uint8_t *buf, uint32_t size)
{
    char path[64];
    FILE *f;
    size_t n = 0;

    snprintf(path, sizeof(path), "tests/data/%s", name);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        n = fread(buf, 1, size, f);
        fclose(f);
    }
    CHECK(n > 0 && n < size);
    return (uint32_t)n;
}

static uint32_t decoded_samples;        /* what play_decoded() had Paula play on each channel */

/* Plays a FLAC ('f') or MP3 ('m') file through Paula, or through the virtual output: the bytes of PCM played */
static uint32_t play_decoded(const char *name, char format, int paula)
{
    static uint8_t file[64 * 1024];
    char fmt[7] = { '1', format, '?', '?', '?', '?', 1 };
    uint32_t len = data_file(name, file, sizeof(file)), played;
    struct player p;

    if (paula)
        setup_paula(&p, AUDIO_PAULA, 0);
    else {
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
    }
    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(&p);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    stub_feed_stream(file, (int)len);
    drain_stream(&p);
    if (paula) {
        output_tick(&p, stub_now);
        paula_play_out(&p);
        played = audio_played(&p, stub_now, 0);
        decoded_samples = stub_paula_out_len;
        CHECK(stub_paula_bad == 0);
    } else {
        stub_stream_end();
        stream_io(&p, PLAT_READ);
        play_for(&p, 10000);
        played = (uint32_t)p.out.handed;
    }
    CHECK(p.out.state == OUTPUT_STOPPED);
    teardown(&p);
    return played;
}

/* A decoded track's format is known only once its decoder has read it: strm s guesses 44.1 kHz stereo. Mono at
 * 22.05 kHz, which Paula plays undivided, takes twice the samples per byte: a block sized for the guess would
 * overrun Paula's buffers. Every sample played, within them; an MP3 stream turning from stereo to mono too, played
 * as the virtual output plays it */
static void test_paula_decoded(void)
{
    CHECK(play_decoded("m16-22k-l5.flac", 'f', 1) == 22050 * 2 && decoded_samples == 22050);
#ifdef AMISQUEEZE_MP3
    CHECK(play_decoded("m16-22k-64.mp3", 'm', 1) == 22050 * 2 && decoded_samples == 22050);
    CHECK(play_decoded("s16-m16-22k.mp3", 'm', 1) == play_decoded("s16-m16-22k.mp3", 'm', 0));
#endif
}

/* The volume is Paula's, set at once; the samples are never scaled; 14 bits at full volume or fixed only */
static void test_paula_volume(void)
{
    static const struct { int adjust; uint32_t left, right; int high, low; } steps[] = {
        { 1, 0x8000, 0x4000, 32, 0 }, { 1, 0, 0, 0, 0 }, { 1, 1, 0, 1, 0 }, { 1, 0xFFFF, 0xFFFF, 64, 0 },
        { 1, 0x10000, 0x10000, 64, 1 }, { 1, 0x100, 0x100, 1, 0 }, { 0, 0x100, 0x100, 64, 1 },
    };
    struct player p;
    uint32_t s, samples;

    fill(track_a, sizeof(track_a), 15);
    setup_paula(&p, AUDIO_PAULA, 0);
    open_paula_stream(&p, '1', '3', '2', '1');
    for (s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        feed_audg(steps[s].adjust, steps[s].left, steps[s].right);
        drain(&p);
        CHECK(stub_paula_high == steps[s].high && stub_paula_low == steps[s].low);
        CHECK(stub_paula_volumes == (int)s + 1);
    }
    feed_audg(1, 0x8000, 0x8000);
    drain(&p);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    paula_play_out(&p);
    samples = ref_paula(track_a, sizeof(track_a) / 4, 2, 0, 2, 0);
    CHECK(played_ref(4, samples));
    CHECK(stub_paula_bad == 0);
    teardown(&p);
}

/* audio.device cannot be opened: the blocks freed, the message printed */
static void test_paula_open_fails(void)
{
    struct player p;
    int b;

    setup(&p, "Amiga");
    stub_paula_fails = 1;
    CHECK(!audio_open(&p, AUDIO_PAULA, 0, 0) && strstr(stub_out, "cannot open audio.device"));
    for (b = 0; b < AUDIO_BLOCKS; b++)
        CHECK(p.audio.block[b].buf == NULL);
    stub_paula_fails = 0;
    teardown(&p);
}

void tests_paula(void)
{
    test_paula_rates();
    test_paula_period();
    test_paula_volume_map();
    test_paula_convert();
    test_paula_convert_values();
    test_paula_play();
    test_paula_pause();
    test_paula_mono_ntsc();
    test_paula_formats();
    test_paula_decoded();
    test_paula_stop();
    test_paula_volume();
    test_paula_open_fails();
}
